//project headers:
#include "EvaluableNodeManagement.h"

//system headers:
#include <limits>
#include <string>
#include <vector>
#include <utility>

const double EvaluableNodeManager::allocExpansionFactor = 1.5;
const ExecutionCycleCountCompactDelta EvaluableNodeManager::minCycleCountBetweenGarbageCollects = 150000;

EvaluableNodeManager::EvaluableNodeManager()
{
	firstUnusedNodeIndex = 0;
	executionCyclesSinceLastGarbageCollection = 0;
}

EvaluableNodeManager::~EvaluableNodeManager()
{
#ifdef MULTITHREAD_SUPPORT
	Concurrency::WriteLock lock(managerAttributesMutex);
#endif

	for(auto &n : nodes)
		delete n;
}

EvaluableNode *EvaluableNodeManager::AllocNode(EvaluableNode *original, EvaluableNodeMetadataModifier metadata_modifier)
{
	EvaluableNode *n = AllocUninitializedNode();
	n->InitializeType(original, metadata_modifier == ENMM_NO_CHANGE, metadata_modifier != ENMM_REMOVE_ALL);

	if(metadata_modifier == ENMM_LABEL_ESCAPE_INCREMENT)
	{
		size_t num_labels = original->GetNumLabels();
		n->ReserveLabels(num_labels);

		//add # in front
		for(size_t i = 0; i < num_labels; i++)
		{
			std::string label = "#" + original->GetLabel(i);
			n->AppendLabel(label);
		}
	}
	else if(metadata_modifier == ENMM_LABEL_ESCAPE_DECREMENT)
	{
		size_t num_labels = original->GetNumLabels();
		n->ReserveLabels(num_labels);

		//remove # in front
		for(size_t i = 0; i < num_labels; i++)
		{
			std::string label = original->GetLabel(i);
			if(label.size() > 0 && label[0] == '#')
				label = label.substr(1);

			n->AppendLabel(label);
		}
	}

	return n;
}

EvaluableNode *EvaluableNodeManager::AllocListNodeWithOrderedChildNodes(EvaluableNodeType child_node_type, size_t num_child_nodes)
{
	size_t num_allocated = 0;
	size_t num_to_alloc = num_child_nodes + 1;

	EvaluableNode *retval = nullptr;

	//start off allocating the parent node, then switch to child_node_type
	EvaluableNodeType cur_type = ENT_LIST;

	//ordered child nodes destination; preallocate outside of the lock (for performance) and swap in
	std::vector<EvaluableNode *> *ocn_ptr = nullptr;
	std::vector<EvaluableNode *> ocn_buffer;
	ocn_buffer.resize(num_child_nodes);

	//outer loop needed for multithreading, but doesn't hurt anything for single threading
	while(num_allocated < num_to_alloc)
	{

	#ifdef MULTITHREAD_SUPPORT
		//attempt to allocate as many as possible using an atomic without write locking
		Concurrency::ReadLock lock(managerAttributesMutex);
	#endif

		for(; num_allocated < num_to_alloc; num_allocated++)
		{
			//attempt to allocate a node and make sure it's valid
			size_t allocated_index = firstUnusedNodeIndex++;
			if(allocated_index < nodes.size())
			{
				if(nodes[allocated_index] != nullptr)
				{
					//before releasing the lock, make sure it has an allocated type, otherwise it could get grabbed by another thread
					nodes[allocated_index]->InitializeType(cur_type);
				}
				else //allocate if nullptr
					nodes[allocated_index] = new EvaluableNode(cur_type);

				//if first node, populate the parent node
				if(num_allocated == 0)
				{
					//prep parent node
					retval = nodes[allocated_index];

					//get the pointer to place child elements,
					// but swap out the preallocated ordered child nodes
					ocn_ptr = &retval->GetOrderedChildNodes();
					std::swap(ocn_buffer, *ocn_ptr);

					//advance type to child node type
					cur_type = child_node_type;
				}
				else //set the appropritae child node
				{
					(*ocn_ptr)[num_allocated - 1] = nodes[allocated_index];
				}
			}
			else
			{
				//the node wasn't valid; put it back and do a write lock to allocate more
				--firstUnusedNodeIndex;
				break;
			}
		}

		//if have allocated enough, just return
		if(num_allocated == num_to_alloc)
			return retval;

	#ifdef MULTITHREAD_SUPPORT

		//don't have enough nodes, so need to attempt a write lock to allocate more
		lock.unlock();
		Concurrency::WriteLock write_lock(managerAttributesMutex);

		//try again after write lock to allocate a node in case another thread has performed the allocation
		//already have the write lock, so don't need to worry about another thread stealing firstUnusedNodeIndex
	#endif

		size_t num_nodes = nodes.size();
		size_t num_nodes_needed = firstUnusedNodeIndex + (num_to_alloc - num_allocated);

		//if don't currently have enough free nodes to meet the needs, then expand the allocation
		if(num_nodes_needed > num_nodes)
		{
			size_t nodes_to_allocate = static_cast<size_t>(allocExpansionFactor * num_nodes_needed) + 1;

			//fill new EvaluableNode slots with nullptr
			nodes.resize(num_nodes + nodes_to_allocate, nullptr);
		}
	}

	//shouldn't make it here
	return retval;
}

bool EvaluableNodeManager::RecommendGarbageCollection()
{
	//makes sure to perform garbage collection between every opcode to find memory reference errors
#ifdef PEDANTIC_GARBAGE_COLLECTION
	return true;
#endif

#ifdef MULTITHREAD_SUPPORT
	if(executionCyclesSinceLastGarbageCollection > minCycleCountBetweenGarbageCollects * static_cast<ExecutionCycleCount>(Concurrency::threadPool.GetNumActiveThreads()))
#else
	if(executionCyclesSinceLastGarbageCollection > minCycleCountBetweenGarbageCollects)
#endif
	{
		auto cur_size = GetNumberOfUsedNodes();

		size_t next_expansion_size = static_cast<size_t>(cur_size * allocExpansionFactor);
		if(next_expansion_size < nodes.size())
		{
			executionCyclesSinceLastGarbageCollection = 0;
			return false;
		}

		return true;
	}

	return false;
}

#ifdef MULTITHREAD_SUPPORT
void EvaluableNodeManager::CollectGarbage(Concurrency::ReadLock *memory_modification_lock)
#else
void EvaluableNodeManager::CollectGarbage()
#endif
{
	if(!RecommendGarbageCollection())
		return;

#ifdef MULTITHREAD_SUPPORT
		
	//free lock so can attempt to enter write lock to collect garbage
	if(memory_modification_lock != nullptr)
		memory_modification_lock->unlock();

	//keep trying to acquire write lock to see if this thread wins the race to collect garbage
	Concurrency::WriteLock write_lock(memoryModificationMutex, std::defer_lock);
	do
	{
		if(!RecommendGarbageCollection())
		{
			if(memory_modification_lock != nullptr)
				memory_modification_lock->lock();
			return;
		}

	} while(!write_lock.try_lock());
		
	//double-check still needs collection, and not that another thread collected it
	if(!RecommendGarbageCollection())
	{
		write_lock.unlock();
		if(memory_modification_lock != nullptr)
			memory_modification_lock->lock();
		return;
	}
#endif

	//perform garbage collection
	FreeAllNodesExceptReferencedNodes();

#ifdef MULTITHREAD_SUPPORT
	//free the unique lock and reacquire the shared lock
	write_lock.unlock();
	if(memory_modification_lock != nullptr)
		memory_modification_lock->lock();
#endif
}

void EvaluableNodeManager::FreeAllNodes()
{
	//get rid of any extra memory
	for(size_t i = 0; i < firstUnusedNodeIndex; i++)
		nodes[i]->Invalidate();

#ifdef MULTITHREAD_SUPPORT
	Concurrency::WriteLock lock(managerAttributesMutex);
#endif

	firstUnusedNodeIndex = 0;
	
	//update details since last garbage collection
	executionCyclesSinceLastGarbageCollection = 0;
}

EvaluableNode *EvaluableNodeManager::AllocUninitializedNode()
{
#ifdef MULTITHREAD_SUPPORT
	//attempt to allocate using an atomic without write locking
	Concurrency::ReadLock lock(managerAttributesMutex);
	
	//attempt to allocate a node and make sure it's valid
	size_t allocated_index = firstUnusedNodeIndex++;
	if(allocated_index < nodes.size())
	{
		if(nodes[allocated_index] != nullptr)
		{
			//before releasing the lock, make sure it has an allocated type, otherwise it could get grabbed by another thread
			nodes[allocated_index]->InitializeUnallocated();
		}
		else //allocate if nullptr
			nodes[allocated_index] = new EvaluableNode();

		return nodes[allocated_index];
	}
	//the node wasn't valid; put it back and do a write lock to allocate more
	--firstUnusedNodeIndex;

	//don't have enough nodes, so need to attempt a write lock to allocate more
	lock.unlock();
	Concurrency::WriteLock write_lock(managerAttributesMutex);

	//try again after write lock to allocate a node in case another thread has performed the allocation
	//already have the write lock, so don't need to worry about another thread stealing firstUnusedNodeIndex
#endif

	size_t num_nodes = nodes.size();
	if(num_nodes > firstUnusedNodeIndex)
	{
		if(nodes[firstUnusedNodeIndex] != nullptr)
		{
		#ifdef MULTITHREAD_SUPPORT
			//before releasing the lock, make sure it has an allocated type, otherwise it could get grabbed by another thread
			nodes[firstUnusedNodeIndex]->InitializeUnallocated();
		#endif	
		}
		else //allocate if nullptr
			nodes[firstUnusedNodeIndex] = new EvaluableNode();

		return nodes[firstUnusedNodeIndex++];
	}

	//ran out, so need another node; push a bunch on the heap so don't need to reallocate as often and slow down garbage collection
	size_t nodes_to_allocate = static_cast<size_t>(allocExpansionFactor * num_nodes) + 1; //preallocate additional resources, plus current node
	
	//fill new EvaluableNode slots with nullptr
	nodes.resize(num_nodes + nodes_to_allocate, nullptr);

	nodes[firstUnusedNodeIndex] = new EvaluableNode();
	return nodes[firstUnusedNodeIndex++];
}

void EvaluableNodeManager::FreeAllNodesExceptReferencedNodes()
{
	if(nodes.size() == 0)
		return;

	uint8_t cur_gc_collect_iteration = 1;

	//set to contain everything that is referenced
	SetAllReferencedNodesGCCollectIteration(cur_gc_collect_iteration);

	//start with a clean slate, and swap everything in use into the in-use region
	size_t lowest_known_unused_index = firstUnusedNodeIndex;	//will store any unused nodes up here; start at what was previously known to be the max, as those above don't need to be rechecked
	//clear firstUnusedNodeIndex to signal to other threads that they won't need to do garbage collection
	firstUnusedNodeIndex = 0;

	//create a temporary variable for multithreading as to not use the atomic variable to slow things down
	size_t first_unused_node_index_temp = 0;
	while(first_unused_node_index_temp < lowest_known_unused_index)
	{
		//nodes can't be nullptr below firstUnusedNodeIndex
		auto &cur_node_ptr = nodes[first_unused_node_index_temp];

		//if the node has been found on this iteration and set to the current iteration count, then move on
		if(cur_node_ptr->GetGarbageCollectionIteration() == cur_gc_collect_iteration)
		{
			first_unused_node_index_temp++;
		}
		else //collect the node
		{
			//free any extra memory used, since this node is no longer needed
			if(cur_node_ptr->GetType() != ENT_DEALLOCATED)
				cur_node_ptr->Invalidate();

			//see if out of things to free; if so exit early
			if(lowest_known_unused_index == 0)
				break;

			//put the node up at the top where unused memory resides and reduce lowest_known_unused_index
			std::swap(cur_node_ptr, nodes[--lowest_known_unused_index]);
		}
	}

	//assign back to the atomic variable
	firstUnusedNodeIndex = first_unused_node_index_temp;

	//reset garbage collection iteration as it has been counted as referenced
	//set to contain everything that is referenced, which could be borrowed nodes from outside of the entity
	// which is why it can't just iterate over nodes
	SetAllReferencedNodesGCCollectIteration(0);

	//update details since last garbage collection
	executionCyclesSinceLastGarbageCollection = 0;
}

void EvaluableNodeManager::FreeNodeTreeRecurse(EvaluableNode *tree)
{
	if(tree->IsAssociativeArray())
	{
		for(auto &[_, e] : tree->GetMappedChildNodesReference())
		{
			if(e != nullptr)
				FreeNodeTreeRecurse(e);
		}
	}
	else
	{
		for(auto &e : tree->GetOrderedChildNodes())
		{
			if(e != nullptr)
				FreeNodeTreeRecurse(e);
		}
	}

	tree->Invalidate();
}

void EvaluableNodeManager::FreeNodeTreeWithCyclesRecurse(EvaluableNode *tree)
{
	if(tree->IsAssociativeArray())
	{
		//pull the mapped child nodes out of the tree before invalidating it
		//need to invalidate before call child nodes to prevent infinite recrusion loop
		EvaluableNode::AssocType mcn;
		auto &tree_mcn = tree->GetMappedChildNodesReference();
		std::swap(mcn, tree_mcn);
		tree->Invalidate();

		for(auto &[_, e] : mcn)
		{
			if(e != nullptr && e->GetType() != ENT_DEALLOCATED)
				FreeNodeTreeWithCyclesRecurse(e);
		}

		//free the references
		string_intern_pool.DestroyStringReferences(mcn, [](auto n) { return n.first; });
	}
	else if(tree->IsImmediate())
	{
		tree->Invalidate();
	}
	else //ordered
	{
		//pull the ordered child nodes out of the tree before invalidating it
		//need to invalidate before call child nodes to prevent infinite recrusion loop
		std::vector<EvaluableNode *> ocn;
		auto &tree_ocn = tree->GetOrderedChildNodesReference();
		std::swap(ocn, tree_ocn);
		tree->Invalidate();

		for(auto &e : ocn)
		{
			if(e != nullptr && e->GetType() != ENT_DEALLOCATED)
				FreeNodeTreeWithCyclesRecurse(e);
		}
	}
}

void EvaluableNodeManager::ModifyLabels(EvaluableNode *n, EvaluableNodeMetadataModifier metadata_modifier)
{
	size_t num_labels = n->GetNumLabels();
	if(num_labels == 0)
		return;

	if(metadata_modifier == ENMM_NO_CHANGE)
		return;

	if(metadata_modifier == ENMM_REMOVE_ALL)
	{
		n->ClearLabels();
		n->ClearComments();
		return;
	}

	if(num_labels == 1)
	{
		std::string label_string = n->GetLabel(0);
		n->ClearLabels();

		if(metadata_modifier == ENMM_LABEL_ESCAPE_INCREMENT)
		{
			label_string.insert(begin(label_string), '#');
			n->AppendLabel(label_string);
		}
		else if(metadata_modifier == ENMM_LABEL_ESCAPE_DECREMENT)
		{
			//remove # in front
			if(label_string.size() > 0 && label_string[0] == '#')
				label_string.erase(begin(label_string));

			n->AppendLabel(label_string);
		}
	}

	//remove all labels and turn into strings
	auto string_labels = n->GetLabelsStrings();
	n->ClearLabels();

	if(metadata_modifier == ENMM_LABEL_ESCAPE_INCREMENT)
	{
		//add # in front
		for(auto &label : string_labels)
			n->AppendLabel("#" + label);
	}
	else if(metadata_modifier == ENMM_LABEL_ESCAPE_DECREMENT)
	{
		//remove # in front
		for(auto &label : string_labels)
		{
			if(label.size() > 0 && label[0] == '#')
				label = label.substr(1);

			n->AppendLabel(label);
		}
	}
}

void EvaluableNodeManager::KeepNodeReference(EvaluableNode *en)
{
	if(en == nullptr)
		return;

#ifdef MULTITHREAD_SUPPORT
	Concurrency::WriteLock lock(managerAttributesMutex);
#endif

	//attempt to put in value 1 for the reference
	auto [inserted_entry, inserted] = nodesCurrentlyReferenced.insert(std::make_pair(en, 1));

	//if couldn't insert because already referenced, then increment
	if(!inserted)
		inserted_entry->second++;
}

void EvaluableNodeManager::FreeNodeReference(EvaluableNode *en)
{
	if(en == nullptr)
		return;

#ifdef MULTITHREAD_SUPPORT
	Concurrency::WriteLock lock(managerAttributesMutex);
#endif

	//get reference count
	auto node = nodesCurrentlyReferenced.find(en);

	//don't do anything if not counted
	if(node == nodesCurrentlyReferenced.end())
		return;

	//if it has sufficient refcount, then just decrement
	if(node->second > 1)
		node->second--;
	else //otherwise remove reference
		nodesCurrentlyReferenced.erase(node);
}

void EvaluableNodeManager::CompactAllocatedNodes()
{
#ifdef MULTITHREAD_SUPPORT
	Concurrency::WriteLock write_lock(managerAttributesMutex);
#endif

	size_t lowest_known_unused_index = firstUnusedNodeIndex;	//store any unused nodes here

	//start with a clean slate, and swap everything in use into the in-use region
	firstUnusedNodeIndex = 0;

	//just in case empty
	if(nodes.size() == 0)
		return;

	while(firstUnusedNodeIndex < lowest_known_unused_index)
	{
		if(nodes[firstUnusedNodeIndex] != nullptr && nodes[firstUnusedNodeIndex]->GetType() != ENT_DEALLOCATED)
			firstUnusedNodeIndex++;
		else
		{
			//see if out of things to free; if so exit early
			if(lowest_known_unused_index == 0)
				break;

			//put the node up at the edge of unused memory, grab the next lowest node and pull it down to increase density
			std::swap(nodes[firstUnusedNodeIndex], nodes[lowest_known_unused_index - 1]);
			lowest_known_unused_index--;
		}
	}
}

size_t EvaluableNodeManager::GetEstimatedTotalReservedSizeInBytes()
{
#ifdef MULTITHREAD_SUPPORT
	Concurrency::ReadLock lock(managerAttributesMutex);
#endif

	size_t total_size = 0;
	for(auto &a : nodes)
		total_size += EvaluableNode::GetEstimatedNodeSizeInBytes(a);
	
	return total_size;
}

size_t EvaluableNodeManager::GetEstimatedTotalUsedSizeInBytes()
{
#ifdef MULTITHREAD_SUPPORT
	Concurrency::ReadLock lock(managerAttributesMutex);
#endif

	size_t total_size = 0;
	for(size_t i = 0; i < firstUnusedNodeIndex; i++)
		total_size += EvaluableNode::GetEstimatedNodeSizeInBytes(nodes[i]);

	return total_size;
}

void EvaluableNodeManager::ValidateEvaluableNodeTreeMemoryIntegrity(EvaluableNode *en)
{
	if(en == nullptr)
		return;

	static EvaluableNode::ReferenceSetType checked;
	checked.clear();
	return ValidateEvaluableNodeTreeMemoryIntegrityRecurse(en, checked);
}

std::pair<EvaluableNode *, bool> EvaluableNodeManager::DeepAllocCopy(EvaluableNode *tree, DeepAllocCopyParams &dacp)
{
	//attempt to insert a new reference for this node, start with null
	auto [inserted_copy, inserted] = dacp.references->insert(std::make_pair(tree, nullptr));

	//can't insert, so already have a copy
	// need to indicate that it has a cycle
	if(!inserted)
		return std::make_pair(inserted_copy->second, true);

	EvaluableNode *copy = AllocNode(tree, dacp.labelModifier);

	//shouldn't happen, but just to be safe
	if(copy == nullptr)
		return std::make_pair(nullptr, false);

	//start without needing a cycle check in case it can be cleared
	copy->SetNeedCycleCheck(false);

	//write the value to the iterator from the earlier insert
	inserted_copy->second = copy;

	//copy and update any child nodes
	if(copy->IsAssociativeArray())
	{
		auto &copy_mcn = copy->GetMappedChildNodesReference();
		for(auto &[_, s] : copy_mcn)
		{
			//get current item in list
			EvaluableNode *n = s;
			if(n == nullptr)
				continue;

			//make copy; if need cycle check, then mark it on the parent copy
			auto [child_copy, need_cycle_check] = DeepAllocCopy(n, dacp);
			if(need_cycle_check)
				copy->SetNeedCycleCheck(true);

			//replace item in assoc with copy
			s = child_copy;
		}
	}
	else
	{
		auto &copy_ocn = copy->GetOrderedChildNodes();
		for(size_t i = 0; i < copy_ocn.size(); i++)
		{
			//get current item in list
			EvaluableNode *n = copy_ocn[i];
			if(n == nullptr)
				continue;

			//make copy; if need cycle check, then mark it on the parent copy
			auto [child_copy, need_cycle_check] = DeepAllocCopy(n, dacp);
			if(need_cycle_check)
				copy->SetNeedCycleCheck(true);

			//replace current item in list with copy
			copy_ocn[i] = child_copy;
		}
	}

	return std::make_pair(copy, copy->GetNeedCycleCheck());
}

#ifdef _OPENMP
EvaluableNode *EvaluableNodeManager::NonCycleDeepAllocCopy(EvaluableNode *tree, EvaluableNodeMetadataModifier metadata_modifier, bool parallelize)
#else
EvaluableNode *EvaluableNodeManager::NonCycleDeepAllocCopy(EvaluableNode *tree, EvaluableNodeMetadataModifier metadata_modifier)
#endif
{
	EvaluableNode *copy = nullptr;
	#pragma omp critical
	{
		copy = AllocNode(tree, metadata_modifier);
	}

	if(copy->IsAssociativeArray())
	{
		//for any mapped children, copy and update
		for(auto &[_, s] : copy->GetMappedChildNodesReference())
		{
			//get current item in list
			EvaluableNode *n = s;
			if(n == nullptr)
				continue;

			//replace item in list with copy
		#ifdef _OPENMP
			s = NonCycleDeepAllocCopy(n, metadata_modifier, parallelize);
		#else
			s = NonCycleDeepAllocCopy(n, metadata_modifier);
		#endif
		}
	}
	else if(!copy->IsImmediate())
	{
		//for any ordered children, copy and update
		auto &copy_ocn = copy->GetOrderedChildNodesReference();

		#pragma omp parallel for schedule(static) if(parallelize && copy->GetOrderedChildNodes().size() > 16)
		for(int64_t i = 0; i < static_cast<int64_t>(copy_ocn.size()); i++)
		{
			//get current item in list
			EvaluableNode *n = copy_ocn[i];
			if(n == nullptr)
				continue;

			//replace current item in list with copy
		#ifdef _OPENMP
			copy_ocn[i] = NonCycleDeepAllocCopy(n, metadata_modifier, parallelize);
		#else
			copy_ocn[i] = NonCycleDeepAllocCopy(n, metadata_modifier);
		#endif
		}
	}

	return copy;
}

void EvaluableNodeManager::ModifyLabelsForNodeTree(EvaluableNode *tree, EvaluableNode::ReferenceSetType &checked, EvaluableNodeMetadataModifier metadata_modifier)
{
	//attempt to insert; if new, mark as not needing a cycle check yet
	// though that may be changed when child nodes are evaluated below
	auto [_, inserted] = checked.insert(tree);
	if(inserted)
		tree->SetNeedCycleCheck(false);
	else //already exists, nothing to do
		return;

	ModifyLabels(tree, metadata_modifier);

	if(tree->IsAssociativeArray())
	{
		for(auto &[cn_id, cn] : tree->GetMappedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			ModifyLabelsForNodeTree(cn, checked, metadata_modifier);
		}
	}
	else if(!tree->IsImmediate())
	{
		for(auto cn : tree->GetOrderedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			ModifyLabelsForNodeTree(cn, checked, metadata_modifier);
		}		
	}
}

void EvaluableNodeManager::NonCycleModifyLabelsForNodeTree(EvaluableNode *tree, EvaluableNodeMetadataModifier metadata_modifier)
{
	ModifyLabels(tree, metadata_modifier);

	if(tree->IsAssociativeArray())
	{
		for(auto &[_, cn] : tree->GetMappedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			NonCycleModifyLabelsForNodeTree(cn, metadata_modifier);
		}
	}
	else if(!tree->IsImmediate())
	{
		for(auto cn : tree->GetOrderedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			NonCycleModifyLabelsForNodeTree(cn, metadata_modifier);
		}
	}
}

std::pair<bool, bool> EvaluableNodeManager::UpdateFlagsForNodeTreeRecurse(EvaluableNode *tree, EvaluableNode::ReferenceSetType &checked)
{
	//attempt to insert; if new, mark as not needing a cycle check yet
	// though that may be changed when child nodes are evaluated below
	auto [_, inserted] = checked.insert(tree);
	if(inserted)
		tree->SetNeedCycleCheck(false);
	else //already exists, notify caller
		return std::make_pair(true, tree->GetIsIdempotent());

	bool is_idempotent = (IsEvaluableNodeTypePotentiallyIdempotent(tree->GetType()) && (tree->GetNumLabels() == 0));
	
	if(tree->IsAssociativeArray())
	{
		bool need_cycle_check = false;

		for(auto &[cn_id, cn] : tree->GetMappedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			auto [cn_need_cycle_check, cn_is_idempotent] = UpdateFlagsForNodeTreeRecurse(cn, checked);

			//update flags for tree
			if(cn_need_cycle_check)
				need_cycle_check = true;

			if(!cn_is_idempotent)
				is_idempotent = false;
		}

		tree->SetNeedCycleCheck(need_cycle_check);
		tree->SetIsIdempotent(is_idempotent);
		return std::make_pair(need_cycle_check, is_idempotent);
	}
	else if(!tree->IsImmediate())
	{
		bool need_cycle_check = false;

		for(auto cn : tree->GetOrderedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			auto [cn_need_cycle_check, cn_is_idempotent] = UpdateFlagsForNodeTreeRecurse(cn, checked);

			//update flags for tree
			if(cn_need_cycle_check)
				need_cycle_check = true;

			if(!cn_is_idempotent)
				is_idempotent = false;
		}

		tree->SetNeedCycleCheck(need_cycle_check);
		tree->SetIsIdempotent(is_idempotent);
		return std::make_pair(need_cycle_check, is_idempotent);
	}
	else //immediate value
	{
		tree->SetIsIdempotent(is_idempotent);
		tree->SetNeedCycleCheck(false);
		return std::make_pair(false, is_idempotent);
	}
}

void EvaluableNodeManager::SetAllReferencedNodesGCCollectIterationRecurse(EvaluableNode *tree, uint8_t gc_collect_iteration)
{
	//if entering this function, then the node hasn't been marked yet
	tree->SetGarbageCollectionIteration(gc_collect_iteration);

	if(tree->IsAssociativeArray())
	{
		for(auto &[_, e] : tree->GetMappedChildNodesReference())
		{
			if(e == nullptr || e->GetGarbageCollectionIteration() == gc_collect_iteration)
				continue;

			SetAllReferencedNodesGCCollectIterationRecurse(e, gc_collect_iteration);
		}
	}
	else if(!tree->IsImmediate())
	{
		for(auto &e : tree->GetOrderedChildNodesReference())
		{
			if(e == nullptr || e->GetGarbageCollectionIteration() == gc_collect_iteration)
				continue;

			SetAllReferencedNodesGCCollectIterationRecurse(e, gc_collect_iteration);
		}
	}	
}

void EvaluableNodeManager::ValidateEvaluableNodeTreeMemoryIntegrityRecurse(EvaluableNode *en, EvaluableNode::ReferenceSetType &checked)
{
	auto [_, inserted] = checked.insert(en);
	if(!inserted)
		return;

	if(en->GetType() == ENT_DEALLOCATED)
		assert(false);

	if(en->IsAssociativeArray())
	{
		for(auto &[cn_id, cn] : en->GetMappedChildNodes())
		{
			if(cn == nullptr)
				continue;

			ValidateEvaluableNodeTreeMemoryIntegrityRecurse(cn, checked);
		}
	}
	else if(!en->IsImmediate())
	{
		for(auto cn : en->GetOrderedChildNodesReference())
		{
			if(cn == nullptr)
				continue;

			ValidateEvaluableNodeTreeMemoryIntegrityRecurse(cn, checked);
		}
	}	
}
