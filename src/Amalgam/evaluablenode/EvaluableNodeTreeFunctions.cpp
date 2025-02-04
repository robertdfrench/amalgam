//project headers:
#include "EvaluableNodeTreeFunctions.h"
#include "FastMath.h"
#include "Interpreter.h"

//system headers:
#include <algorithm>
#include <cctype>

bool CustomEvaluableNodeComparator::operator()(EvaluableNode *a, EvaluableNode *b)
{
	//create context with "a" and "b" variables
	interpreter->PushNewConstructionContext(nullptr, targetList, EvaluableNodeImmediateValueWithType(), a);
	interpreter->PushNewConstructionContext(nullptr, targetList, EvaluableNodeImmediateValueWithType(), b);

	//compare
	bool retval = (interpreter->InterpretNodeIntoNumberValue(function) > 0);

	interpreter->PopConstructionContext();
	interpreter->PopConstructionContext();

	return retval;
}

//performs a top-down stable merge on the sub-lists from start_index to middle_index and middle_index to _end_index from source into destination using cenc
void CustomEvaluableNodeOrderedChildNodesTopDownMerge(std::vector<EvaluableNode *> &source, size_t start_index, size_t middle_index, size_t end_index, std::vector<EvaluableNode *> &destination, CustomEvaluableNodeComparator &cenc)
{
	size_t left_pos = start_index;
	size_t right_pos = middle_index;

	//for all elements, pull from the appropriate buffer (left or right)
	for(size_t cur_index = start_index; cur_index < end_index; cur_index++)
	{
		//if left_pos has elements left and is less than the right, use it
		if(left_pos < middle_index && (right_pos >= end_index || cenc(source[left_pos], source[right_pos])))
		{
			destination[cur_index] = source[left_pos];
			left_pos++;
		}
		else //the right is less, use that
		{
			destination[cur_index] = source[right_pos];
			right_pos++;
		}
	}
}

//performs a stable merge sort of source (which *will* be modified and is not constant) from start_index to end_index into destination; uses cenc for comparison
void CustomEvaluableNodeOrderedChildNodesSort(std::vector<EvaluableNode *> &source, size_t start_index, size_t end_index, std::vector<EvaluableNode *> &destination, CustomEvaluableNodeComparator &cenc)
{
	//if one element, then sorted
	if(start_index + 1 >= end_index)
		return;

	size_t middle_index = (start_index + end_index) / 2;

	//sort left into list
	CustomEvaluableNodeOrderedChildNodesSort(destination, start_index, middle_index, source, cenc);
	//sort right into list
	CustomEvaluableNodeOrderedChildNodesSort(destination, middle_index, end_index, source, cenc);

	//merge buffers back into buffer
	CustomEvaluableNodeOrderedChildNodesTopDownMerge(source, start_index, middle_index, end_index, destination, cenc);
}

std::vector<EvaluableNode *> CustomEvaluableNodeOrderedChildNodesSort(std::vector<EvaluableNode *> &list, CustomEvaluableNodeComparator &cenc)
{
	//must make two copies of the list to edit, because switch back and forth and there is a chance that an element may be invalid
	// in either list.  Therefore, can't use the original list in the off chance that something is garbage collected
	std::vector<EvaluableNode *> list_copy_1(list);
	std::vector<EvaluableNode *> list_copy_2(list);
	CustomEvaluableNodeOrderedChildNodesSort(list_copy_1, 0, list.size(), list_copy_2, cenc);
	return list_copy_2;
}

//compares right-aligned numbers in a string.  searches for first digit that isn't equal,
// figures out which one is greater, and remembers it.  then it sees which number string is longer
// if the number strings are the same length, then go with whichever was remembered to be bigger
// both indices will be updated along the way
int CompareNumberInStringRightJustified(const std::string &a, const std::string &b, size_t &a_index, size_t &b_index)
{
	//comparison result of first non-matching digit
	int compare_val_if_same_length = 0;

	while(1)
	{
		unsigned char a_value;
		unsigned char b_value;

		//treat as if zero terminated strings
		if(a_index < a.size())
			a_value = a[a_index];
		else
			a_value = '\0';

		if(b_index < b.size())
			b_value = b[b_index];
		else
			b_value = '\0';

		if(!std::isdigit(a_value) && !std::isdigit(b_value))
			return compare_val_if_same_length;
		if(!std::isdigit(a_value))
			return -1;
		if(!std::isdigit(b_value))
			return +1;
		
		//see if found first nonmatching digit
		if(a_value < b_value)
		{
			if(compare_val_if_same_length == 0)
				compare_val_if_same_length = -1;
		}
		else if(a_value > b_value)
		{
			if(compare_val_if_same_length == 0)
				compare_val_if_same_length = +1;
		}

		a_index++;
		b_index++;
	}

	//can't make it here
	return 0;
}


//compares left-aligned numbers in a string until a difference is found, then uses that for comparison
// starts at the specified indicies
// both indices will be updated along the way
int CompareNumberInStringLeftJustified(const std::string &a, const std::string &b, size_t &a_index, size_t &b_index)
{
	while(1)
	{
		unsigned char a_value;
		unsigned char b_value;

		//treat as if zero terminated strings
		if(a_index < a.size())
			a_value = a[a_index];
		else
			a_value = '\0';

		if(b_index < b.size())
			b_value = b[b_index];
		else
			b_value = '\0';

		//if out of digits, then they're equal
		if(!std::isdigit(a_value) && !std::isdigit(b_value))
			return 0;

		//if one ran out of digits, then it's less
		if(!std::isdigit(a_value))
			return -1;
		if(!std::isdigit(b_value))
			return +1;

		//compare values
		if(a_value < b_value)
			return -1;
		if(a_value > b_value)
			return +1;

		a_index++;
		b_index++;
	}
	
	//can't get here
	return 0;
}

//compares two strings "naturally" as applicable, ignoring spaces and treating numbers how a person would
// however, if the strings are "identical" via natural comparison, then it falls back to regular string comparison to ensure
// that strings are always ordered the same way
int StringNaturalCompare(const std::string &a, const std::string &b)
{
	size_t a_index = 0, b_index = 0;

	while(1)
	{
		unsigned char a_value;
		unsigned char b_value;

		//skip over spaces
		while(a_index < a.size() && std::isspace(static_cast<unsigned char>(a[a_index])))
			a_index++;
		//treat as if zero terminated string
		if(a_index < a.size())
			a_value = a[a_index];
		else
			a_value = '\0';

		//skip over spaces
		while(b_index < b.size() && std::isspace(static_cast<unsigned char>(b[b_index])))
			b_index++;
		if(b_index < b.size())
			b_value = b[b_index];
		else
			b_value = '\0';

		//check for group of digits
		if(std::isdigit(a_value) && std::isdigit(static_cast<unsigned char>(b_value)))
		{
			int result;
			//if starts with leading zeros, then do a comparison from the left, otherwise from the right
			if(a_value == '0' || b_value == '0')
				result = CompareNumberInStringLeftJustified(a, b, a_index, b_index);
			else
				result = CompareNumberInStringRightJustified(a, b, a_index, b_index);

			if(result != 0)
				return result;
			
			//if made it here, then the numbers were equal; move on to the next character
			continue;
		}

		//if strings are identical from a natural sorting perspective, then use regular compare to make sure order consistency is preserved
		if(a_value == '\0' && b_value == '\0')
			return a.compare(b);

		if(a_value < b_value)
			return -1;

		if(a_value > b_value)
			return +1;

		a_index++;
		b_index++;
	}

	return 0;
}

void TraverseToEntityViaEvaluableNodeIDPath(Entity *container, EvaluableNode *id_path, Entity *&relative_entity_parent, StringInternRef &id, Entity *&relative_entity)
{
	//TODO 10975: make this use locks as appropriate
	relative_entity_parent = nullptr;
	id = StringInternPool::NOT_A_STRING_ID;
	relative_entity = nullptr;

	if(container == nullptr)
		return;

	if(EvaluableNode::IsEmptyNode(id_path))
	{
		relative_entity = container;
		return;
	}

	if(id_path->GetOrderedChildNodes().size() == 0)
	{
		id.SetIDWithReferenceHandoff(EvaluableNode::ToStringIDWithReference(id_path));
		relative_entity = container->GetContainedEntity(id);
		relative_entity_parent = container;
		return;
	}
	
	relative_entity_parent = container;
	relative_entity = container;
	for(auto &cn : id_path->GetOrderedChildNodes())
	{
		relative_entity_parent = relative_entity;
		//if id_path is going past the end of what exists, then it is invalid
		if(relative_entity_parent == nullptr)
		{
			relative_entity = nullptr;
			return;
		}

		id.SetIDWithReferenceHandoff(EvaluableNode::ToStringIDWithReference(cn));
		relative_entity = relative_entity_parent->GetContainedEntity(id);
	}
}

void TraverseEntityToNewDestinationViaEvaluableNodeIDPath(Entity *container, EvaluableNode *id_path, Entity *&destination_entity_parent, StringInternRef &destination_id)
{
	//TODO 10975: make this use locks as appropriate
	Entity *destination_entity = nullptr;
	TraverseToEntityViaEvaluableNodeIDPath(container, id_path, destination_entity_parent, destination_id, destination_entity);

	//if it already exists, then place inside it
	if(destination_entity != nullptr)
	{
		destination_entity_parent = destination_entity;
		destination_entity = nullptr;

		destination_id = StringInternRef::EmptyString();
	}

	//if couldn't get the parent, just use the original container
	if(destination_entity_parent == nullptr && destination_id == StringInternPool::NOT_A_STRING_ID)
		destination_entity_parent = container;
}

EvaluableNode *GetTraversalIDPathListFromAToB(EvaluableNodeManager *enm, Entity *a, Entity *b)
{
	//create list to address entity
	EvaluableNode *id_list = enm->AllocNode(ENT_LIST);
	auto &ocn = id_list->GetOrderedChildNodes();
	while(b != nullptr && b != a)
	{
		ocn.push_back(enm->AllocNode(ENT_STRING, b->GetIdStringId()));
		b = b->GetContainer();
	}

	std::reverse(begin(ocn), end(ocn));
	return id_list;
}

EvaluableNode *GetTraversalPathListFromAToB(EvaluableNodeManager *enm, EvaluableNode::ReferenceAssocType &node_parents, EvaluableNode *a, EvaluableNode *b)
{
	if(a == nullptr || b == nullptr)
		return nullptr;

	EvaluableNode *path_list = enm->AllocNode(ENT_LIST);

	//find a path from b back to a by way of parents
	EvaluableNode::ReferenceSetType nodes_visited;
	EvaluableNode *b_ancestor = b;
	EvaluableNode *b_ancestor_parent = node_parents[b_ancestor];

	while(b_ancestor_parent != nullptr
		&& b_ancestor != a		//stop if it's the target
		&& nodes_visited.insert(b_ancestor_parent).second == true) //make sure not visited yet
	{

		//find where the node matches
		if(b_ancestor_parent->IsAssociativeArray())
		{
			//look up which key corresponds to the value
			StringInternPool::StringID key_sid = StringInternPool::NOT_A_STRING_ID;
			for(auto &[s_id, s] : b_ancestor_parent->GetMappedChildNodesReference())
			{
				if(s == b_ancestor)
				{
					key_sid = s_id;
					break;
				}
			}

			path_list->AppendOrderedChildNode(enm->AllocNode(ENT_STRING, key_sid));
		}
		else if(b_ancestor_parent->IsOrderedArray())
		{
			auto &b_ancestor_parent_ocn = b_ancestor_parent->GetOrderedChildNodesReference();
			const auto &found = std::find(begin(b_ancestor_parent_ocn), end(b_ancestor_parent_ocn), b_ancestor);
			auto index = std::distance(begin(b_ancestor_parent_ocn), found);
			path_list->AppendOrderedChildNode(enm->AllocNode(static_cast<double>(index)));
		}
		else //didn't work... odd/error condition
		{
			enm->FreeNodeTree(path_list);
			return nullptr;
		}

		b_ancestor = b_ancestor_parent;
		b_ancestor_parent = node_parents[b_ancestor];
	}

	//if didn't end up hitting our target, then we can't get there
	if(b_ancestor != a)
	{
		enm->FreeNodeTree(path_list);
		return nullptr;
	}
	
	//reverse because assembled in reverse order
	auto &ocn = path_list->GetOrderedChildNodes();
	std::reverse(begin(ocn), end(ocn));
	return path_list;
}

EvaluableNode **GetRelativeEvaluableNodeFromTraversalPathList(EvaluableNode **source, EvaluableNode **index_path_nodes, size_t num_index_path_nodes, EvaluableNodeManager *enm, size_t max_num_nodes)
{
	//walk through address list to find target
	EvaluableNode **destination = source;
	for(size_t i = 0; i < num_index_path_nodes; i++)
	{
		//make sure valid and traversible, since at least one more address will be dereferenced
		if(destination == nullptr)
			break;

		//fetch the new destination based on what is being fetched
		EvaluableNode *addr = index_path_nodes[i];
		bool addr_empty = EvaluableNode::IsEmptyNode(addr);

		//if out of nodes but need to traverse further in the index, then will need to create new nodes
		if((*destination) == nullptr)
		{
			if(enm == nullptr)
			{
				destination = nullptr;
				break;
			}

			//need to create a new node to fill in, but create the most generic type possible that uses the type of the index as the way to access it
			if(!addr_empty && DoesEvaluableNodeTypeUseNumberData(addr->GetType())) //used to access lists
				*destination = enm->AllocNode(ENT_LIST);
			else
				*destination = enm->AllocNode(ENT_ASSOC);
		}

		if(EvaluableNode::IsAssociativeArray(*destination))
		{
			auto &mcn = (*destination)->GetMappedChildNodesReference();

			if(enm == nullptr)
			{
				auto key_sid = StringInternPool::NOT_A_STRING_ID;
				if(!addr_empty)
				{
					//string must already exist if can't create anything
					key_sid = EvaluableNode::ToStringIDIfExists(addr);
					if(key_sid == StringInternPool::NOT_A_STRING_ID)
					{
						destination = nullptr;
						break;
					}
				}

				//try to find key
				auto found = mcn.find(key_sid);
				if(found == end(mcn))
				{
					destination = nullptr;
					break;
				}
				
				destination = &(found->second);
			}
			else //create entry if it doesn't exist
			{
				auto key_sid = EvaluableNode::ToStringIDWithReference(addr);

				//attempt to insert the new key
				auto [inserted_key, inserted] = mcn.insert(std::make_pair(key_sid, nullptr));

				//if not inserted, then destroy the reference
				if(!inserted)
					string_intern_pool.DestroyStringReference(key_sid);

				//regardless of whether or not the result was inserted, grab the value portion
				destination = &(inserted_key->second);
			}
		}
		else if(!addr_empty && EvaluableNode::IsOrderedArray(*destination))
		{
			auto &ocn = (*destination)->GetOrderedChildNodesReference();
			double index = EvaluableNode::ToNumber(addr);
			//if negative, start from end and wrap around if the negative index is larger than the size
			if(index < 0)
			{
				index += ocn.size();
				if(index < 0) //clamp at zero
					index = 0;
			}

			//treat NaNs as 0
			if(FastIsNaN(index))
				index = 0;

			//make sure within bounds
			if(index < ocn.size())
				destination = &(ocn[static_cast<size_t>(index)]);
			else //beyond index
			{
				if(enm == nullptr)
					destination = nullptr;
				else //resize to fit
				{
					//if the index is more than can be referenced in 53 bits of 64-bit float mantissa,
					// then can't deal with it
					if(index >= 9007199254740992)
					{
						destination = nullptr;
						break;
					}

					//find the index and validate it
					size_t new_index = static_cast<size_t>(index);
					//if have specified a maximum number of nodes (not zero), then abide by it
					if(max_num_nodes > 0 && new_index > max_num_nodes)
					{
						destination = nullptr;
						break;
					}

					ocn.resize(new_index + 1, nullptr);
					destination = &(ocn[new_index]);
				}
			}
		}
		else //an immediate value -- can't get anything on the immediate
		{
			destination = nullptr;
		}
	}

	return destination;
}

EvaluableNodeReference AccumulateEvaluableNodeIntoEvaluableNode(EvaluableNodeReference value_destination_node, EvaluableNodeReference variable_value_node, EvaluableNodeManager *enm)
{
	//if the destination is empty, then just use the value specified
	if(value_destination_node.reference == nullptr)
		return variable_value_node;

	//if the value is unique, then can just edit in place
	if(value_destination_node.unique)
	{
		if(EvaluableNode::CanRepresentValueAsANumber(value_destination_node) && !EvaluableNode::IsNaN(value_destination_node))
		{
			double cur_value = EvaluableNode::ToNumber(value_destination_node);
			double inc_value = EvaluableNode::ToNumber(variable_value_node);
			value_destination_node.reference->SetType(ENT_NUMBER, enm);
			value_destination_node->SetNumberValue(cur_value + inc_value);
		}
		else if(value_destination_node->IsAssociativeArray())
		{
			if(EvaluableNode::IsAssociativeArray(variable_value_node))
			{
				value_destination_node->ReserveMappedChildNodes(value_destination_node->GetMappedChildNodesReference().size()
																+ variable_value_node->GetMappedChildNodes().size());
				value_destination_node->AppendMappedChildNodes(variable_value_node->GetMappedChildNodes());
			}
			else if(variable_value_node != nullptr) //treat ordered pairs as new entries as long as not nullptr
			{
				value_destination_node->ReserveMappedChildNodes(value_destination_node->GetMappedChildNodesReference().size()
																+ variable_value_node->GetOrderedChildNodes().size() / 2);

				//iterate as long as pairs exist
				auto &vvn_ocn = variable_value_node->GetOrderedChildNodes();
				for(size_t i = 0; i + 1 < vvn_ocn.size(); i += 2)
				{
					StringInternPool::StringID key_sid = EvaluableNode::ToStringIDWithReference(vvn_ocn[i]);
					value_destination_node->SetMappedChildNodeWithReferenceHandoff(key_sid, vvn_ocn[i + 1]);
				}
			}

			enm->FreeNodeIfPossible(variable_value_node);

			value_destination_node->SetNeedCycleCheck(true);
			value_destination_node.unique = (value_destination_node.unique && variable_value_node.unique);
		}
		else if(value_destination_node->IsStringValue())
		{
			std::string cur_value = EvaluableNode::ToString(value_destination_node);
			std::string inc_value = EvaluableNode::ToString(variable_value_node);
			value_destination_node->SetType(ENT_STRING, enm);
			value_destination_node->SetStringValue(cur_value.append(inc_value));
			value_destination_node.unique = true;
		}
		else //add ordered child node
		{
			if(EvaluableNode::IsAssociativeArray(variable_value_node))
			{
				//expand out into pairs
				value_destination_node->ReserveOrderedChildNodes(value_destination_node->GetOrderedChildNodes().size()
																+ 2 * variable_value_node->GetMappedChildNodesReference().size());
				
				for(auto &[cn_id, cn] : variable_value_node->GetMappedChildNodesReference())
				{
					value_destination_node->AppendOrderedChildNode(enm->AllocNode(ENT_STRING, cn_id));
					value_destination_node->AppendOrderedChildNode(cn);
				}

				enm->FreeNodeIfPossible(variable_value_node);
			}
			else if(EvaluableNode::IsOrderedArray(variable_value_node))
			{
				value_destination_node->ReserveOrderedChildNodes(value_destination_node->GetOrderedChildNodes().size()
																+ variable_value_node->GetOrderedChildNodesReference().size());
				value_destination_node->AppendOrderedChildNodes(variable_value_node->GetOrderedChildNodesReference());

				enm->FreeNodeIfPossible(variable_value_node);
			}
			else //just append one value
			{
				value_destination_node->AppendOrderedChildNode(variable_value_node);
			}

			value_destination_node->SetNeedCycleCheck(true);
			value_destination_node.unique = (value_destination_node.unique && variable_value_node.unique);
		}

		return value_destination_node;
	}

	//not unique, so need to make a new list
	if(EvaluableNode::CanRepresentValueAsANumber(value_destination_node) && !EvaluableNode::IsNaN(value_destination_node))
	{
		double cur_value = EvaluableNode::ToNumber(value_destination_node);
		double inc_value = EvaluableNode::ToNumber(variable_value_node);
		value_destination_node.reference = enm->AllocNode(cur_value + inc_value);
		value_destination_node.unique = true;
	}
	else if(value_destination_node->IsAssociativeArray())
	{
		EvaluableNode *new_list = enm->AllocNode(value_destination_node->GetType());

		if(EvaluableNode::IsAssociativeArray(variable_value_node))
		{
			new_list->ReserveMappedChildNodes(value_destination_node->GetMappedChildNodes().size()
											+ variable_value_node->GetMappedChildNodesReference().size());
			new_list->SetMappedChildNodes(value_destination_node->GetMappedChildNodes(), true);
			new_list->AppendMappedChildNodes(variable_value_node->GetMappedChildNodes());
		}
		else if(variable_value_node != nullptr) //treat ordered pairs as new entries as long as not nullptr
		{
			new_list->ReserveMappedChildNodes(value_destination_node->GetMappedChildNodes().size() + variable_value_node->GetOrderedChildNodes().size() / 2);
			new_list->SetMappedChildNodes(value_destination_node->GetMappedChildNodes(), true);
			//iterate as long as pairs exist
			auto &vvn_ocn = variable_value_node->GetOrderedChildNodes();
			for(size_t i = 0; i + 1 < vvn_ocn.size(); i += 2)
			{
				StringInternPool::StringID key_sid = EvaluableNode::ToStringIDWithReference(vvn_ocn[i]);
				new_list->SetMappedChildNodeWithReferenceHandoff(key_sid, vvn_ocn[i + 1]);
			}
		}

		enm->FreeNodeIfPossible(variable_value_node);

		value_destination_node.reference = new_list;
		value_destination_node->SetNeedCycleCheck(true);
		value_destination_node.unique = (value_destination_node.unique && variable_value_node.unique);
	}
	else if(value_destination_node->IsStringValue())
	{
		std::string cur_value = EvaluableNode::ToString(value_destination_node);
		std::string inc_value = EvaluableNode::ToString(variable_value_node);
		value_destination_node.reference = enm->AllocNode(ENT_STRING, cur_value.append(inc_value));
		value_destination_node.unique = true;
	}
	else //add ordered child node
	{
		EvaluableNode *new_list = enm->AllocNode(ENT_LIST);
		if(EvaluableNode::IsAssociativeArray(variable_value_node))
		{
			//expand out into pairs
			new_list->ReserveOrderedChildNodes(value_destination_node->GetOrderedChildNodes().size() + 2 * variable_value_node->GetMappedChildNodes().size());
			new_list->AppendOrderedChildNodes(value_destination_node->GetOrderedChildNodes());
			for(auto &[cn_id, cn] : variable_value_node->GetMappedChildNodes())
			{
				new_list->AppendOrderedChildNode(enm->AllocNode(ENT_STRING, cn_id));
				new_list->AppendOrderedChildNode(cn);
			}

			enm->FreeNodeIfPossible(variable_value_node);
		}
		else if(EvaluableNode::IsOrderedArray(variable_value_node))
		{
			new_list->ReserveOrderedChildNodes(value_destination_node->GetOrderedChildNodes().size() + variable_value_node->GetOrderedChildNodes().size());
			new_list->AppendOrderedChildNodes(value_destination_node->GetOrderedChildNodes());
			new_list->AppendOrderedChildNodes(variable_value_node->GetOrderedChildNodes());

			enm->FreeNodeIfPossible(variable_value_node);
		}
		else //just append one value
		{
			new_list->ReserveOrderedChildNodes(value_destination_node->GetOrderedChildNodes().size() + 1);
			new_list->AppendOrderedChildNodes(value_destination_node->GetOrderedChildNodes());
			new_list->AppendOrderedChildNode(variable_value_node);
		}

		value_destination_node.reference = new_list;
		value_destination_node->SetNeedCycleCheck(true);
		value_destination_node.unique = (value_destination_node.unique && variable_value_node.unique);
	}

	return value_destination_node;
}
