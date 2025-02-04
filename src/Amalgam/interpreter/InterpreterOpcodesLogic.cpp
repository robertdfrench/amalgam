//project headers:
#include "Interpreter.h"

#include "AmalgamVersion.h"
#include "AssetManager.h"
#include "EntityManipulation.h"
#include "EntityQueries.h"
#include "EntityQueryManager.h"
#include "EvaluableNodeTreeFunctions.h"
#include "EvaluableNodeTreeManipulation.h"
#include "EvaluableNodeTreeDifference.h"
#include "PerformanceProfiler.h"

//system headers:
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <utility>

EvaluableNodeReference Interpreter::InterpretNode_ENT_AND(EvaluableNode *en)
{
	EvaluableNodeReference cur = EvaluableNodeReference::Null();
	auto &ocn = en->GetOrderedChildNodes();

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		for(auto &cn : interpreted_nodes)
		{
			//free the previous node if applicable
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			cur = cn;

			if(!EvaluableNode::IsTrue(cur))
			{
				evaluableNodeManager->FreeNodeTreeIfPossible(cur);
				return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
			}
		}

		return cur;
	}
#endif

	for(auto &cn : ocn)
	{
		//free the previous node if applicable
		evaluableNodeManager->FreeNodeTreeIfPossible(cur);

		cur = InterpretNode(cn);

		if(!EvaluableNode::IsTrue(cur))
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);
			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}
	}
	return cur;
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_OR(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		for(auto &cur : interpreted_nodes)
		{
			//if it is a valid node and it is not zero, then return it
			if(EvaluableNode::IsTrue(cur))
				return cur;

			evaluableNodeManager->FreeNodeTreeIfPossible(cur);
		}

		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
	}
#endif

	for(auto &cn : ocn)
	{
		auto cur = InterpretNode(cn);

		//if it is a valid node and it is not zero, then return it
		if(EvaluableNode::IsTrue(cur))
			return cur;

		evaluableNodeManager->FreeNodeTreeIfPossible(cur);
	}
	return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_XOR(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	size_t num_true = 0;

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		for(auto &cur : interpreted_nodes)
		{
			//if it's true, count it
			if(EvaluableNode::IsTrue(cur))
				num_true++;

			evaluableNodeManager->FreeNodeTreeIfPossible(cur);
		}

		//if an odd number of true arguments, then return true
		return EvaluableNodeReference(evaluableNodeManager->AllocNode((num_true % 2 == 1) ? ENT_TRUE : ENT_FALSE), true);
	}
#endif

	//count number of true values
	for(auto &cn : ocn)
	{
		if(InterpretNodeIntoBoolValue(cn))
			num_true++;
	}

	//if an odd number of true arguments, then return true
	return EvaluableNodeReference(evaluableNodeManager->AllocNode((num_true % 2 == 1) ? ENT_TRUE : ENT_FALSE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_NOT(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	auto cur = InterpretNodeForImmediateUse(ocn[0]);

	bool is_true = EvaluableNode::IsTrue(cur);

	if(cur.unique && cur != nullptr)
		cur->ClearAndSetType(is_true ? ENT_FALSE : ENT_TRUE);
	else
		cur = EvaluableNodeReference(evaluableNodeManager->AllocNode(is_true ? ENT_FALSE : ENT_TRUE), true);

	return cur;
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_EQUAL(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	bool processed_first_value = false;
	EvaluableNodeReference to_match = EvaluableNodeReference::Null();

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		for(auto &cur : interpreted_nodes)
		{
			//if haven't gotten a value yet, then use this as the first data
			if(!processed_first_value)
			{
				to_match = cur;
				processed_first_value = true;
				continue;
			}

			if(!EvaluableNode::AreDeepEqual(to_match, cur))
			{
				evaluableNodeManager->FreeNodeTreeIfPossible(to_match);
				evaluableNodeManager->FreeNodeTreeIfPossible(cur);

				return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
			}

			evaluableNodeManager->FreeNodeTreeIfPossible(cur);
		}

		evaluableNodeManager->FreeNodeTreeIfPossible(to_match);

		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_TRUE), true);
	}
#endif

	auto node_stack = CreateInterpreterNodeStackStateSaver();

	for(auto &cn : ocn)
	{
		auto cur = InterpretNodeForImmediateUse(cn);

		//if haven't gotten a value yet, then use this as the first data
		if(!processed_first_value)
		{
			to_match = cur;
			node_stack.PushEvaluableNode(to_match);
			processed_first_value = true;
			continue;
		}

		if(!EvaluableNode::AreDeepEqual(to_match, cur))
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(to_match);
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		evaluableNodeManager->FreeNodeTreeIfPossible(cur);
	}

	evaluableNodeManager->FreeNodeTreeIfPossible(to_match);

	return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_TRUE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_NEQUAL(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		bool all_not_equal = true;
		for(size_t i = 0; i < interpreted_nodes.size(); i++)
		{
			//don't compare versus self, and skip any previously compared against
			for(size_t j = i + 1; j < interpreted_nodes.size(); j++)
			{
				//if they're equal, then it fails
				if(EvaluableNode::AreDeepEqual(interpreted_nodes[i], interpreted_nodes[j]))
				{
					all_not_equal = false;

					//break out of loop
					i = interpreted_nodes.size();
					break;
				}
			}
		}

		for(size_t i = 0; i < interpreted_nodes.size(); i++)
			evaluableNodeManager->FreeNodeTreeIfPossible(interpreted_nodes[i]);

		return EvaluableNodeReference(evaluableNodeManager->AllocNode(all_not_equal ? ENT_TRUE : ENT_FALSE), true);
	}
#endif

	//special (faster) case for comparing two
	if(ocn.size() == 2)
	{
		EvaluableNodeReference a = InterpretNodeForImmediateUse(ocn[0]);

		auto node_stack = CreateInterpreterNodeStackStateSaver(a);
		EvaluableNodeReference b = InterpretNodeForImmediateUse(ocn[1]);

		bool a_b_not_equal = (!EvaluableNode::AreDeepEqual(a, b));
		evaluableNodeManager->FreeNodeTreeIfPossible(a);
		evaluableNodeManager->FreeNodeTreeIfPossible(b);

		return EvaluableNodeReference(evaluableNodeManager->AllocNode(a_b_not_equal ? ENT_TRUE : ENT_FALSE), true);
	}

	auto node_stack = CreateInterpreterNodeStackStateSaver();

	//get the value for each node
	std::vector<EvaluableNodeReference> values;
	values.reserve(ocn.size());
	for(size_t i = 0; i < ocn.size(); i++)
	{
		values.push_back(InterpretNodeForImmediateUse(ocn[i]));
		node_stack.PushEvaluableNode(values[i]);
	}

	bool all_not_equal = true;
	for(size_t i = 0; i < values.size(); i++)
	{
		//don't compare versus self, and skip any previously compared against
		for(size_t j = i + 1; j < values.size(); j++)
		{
			//if they're equal, then it fails
			if(EvaluableNode::AreDeepEqual(values[i], values[j]))
			{
				all_not_equal = false;

				//break out of loop
				i = values.size();
				break;
			}
		}
	}

	for(size_t i = 0; i < values.size(); i++)
		evaluableNodeManager->FreeNodeTreeIfPossible(values[i]);

	return EvaluableNodeReference(evaluableNodeManager->AllocNode(all_not_equal ? ENT_TRUE : ENT_FALSE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_LESS_and_LEQUAL(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	//if none or one node, then there's no order
	if(ocn.size() < 2)
		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		EvaluableNodeReference prev = interpreted_nodes[0];
		if(EvaluableNode::IsNaN(prev))
		{
			for(auto &n : interpreted_nodes)
				evaluableNodeManager->FreeNodeTreeIfPossible(n);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		EvaluableNodeType return_type = ENT_TRUE;

		for(size_t i = 1; i < interpreted_nodes.size(); i++)
		{
			//if not in strict increasing order, return false
			auto &cur = interpreted_nodes[i];

			if(EvaluableNode::IsNaN(cur))
			{
				return_type = ENT_FALSE;
				break;
			}

			if(!EvaluableNode::IsLessThan(prev, cur, en->GetType() == ENT_LEQUAL))
			{
				return_type = ENT_FALSE;
				break;
			}
		}

		for(auto &n : interpreted_nodes)
			evaluableNodeManager->FreeNodeTreeIfPossible(n);
		
		return EvaluableNodeReference(evaluableNodeManager->AllocNode(return_type), true);
	}
#endif

	auto prev = InterpretNodeForImmediateUse(ocn[0]);
	if(EvaluableNode::IsEmptyNode(prev))
	{
		evaluableNodeManager->FreeNodeTreeIfPossible(prev);
		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
	}
	auto node_stack = CreateInterpreterNodeStackStateSaver(prev);

	for(size_t i = 1; i < ocn.size(); i++)
	{
		//if not in strict increasing order, return false
		auto cur = InterpretNodeForImmediateUse(ocn[i]);

		if(EvaluableNode::IsEmptyNode(cur))
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(prev);
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		if(!EvaluableNode::IsLessThan(prev, cur, en->GetType() == ENT_LEQUAL))
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(prev);
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		evaluableNodeManager->FreeNodeTreeIfPossible(prev);
		prev = cur;

		node_stack.PopEvaluableNode();
		node_stack.PushEvaluableNode(prev);
	}

	evaluableNodeManager->FreeNodeTreeIfPossible(prev);

	//nothing is out of order
	return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_TRUE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_GREATER_and_GEQUAL(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	//if none or one node, then it's in order
	if(ocn.size() < 2)
		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		EvaluableNodeReference prev = interpreted_nodes[0];
		if(EvaluableNode::IsNaN(prev))
		{
			for(auto &n : interpreted_nodes)
				evaluableNodeManager->FreeNodeTreeIfPossible(n);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		EvaluableNodeType return_type = ENT_TRUE;

		for(size_t i = 1; i < interpreted_nodes.size(); i++)
		{
			//if not in strict increasing order, return false
			auto &cur = interpreted_nodes[i];

			if(EvaluableNode::IsNaN(cur))
			{
				return_type = ENT_FALSE;
				break;
			}

			if(!EvaluableNode::IsLessThan(cur, prev, en->GetType() == ENT_GEQUAL))
			{
				return_type = ENT_FALSE;
				break;
			}
		}

		for(auto &n : interpreted_nodes)
			evaluableNodeManager->FreeNodeTreeIfPossible(n);

		return EvaluableNodeReference(evaluableNodeManager->AllocNode(return_type), true);
	}
#endif

	auto prev = InterpretNodeForImmediateUse(ocn[0]);
	if(EvaluableNode::IsEmptyNode(prev))
	{
		evaluableNodeManager->FreeNodeTreeIfPossible(prev);
		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
	}

	auto node_stack = CreateInterpreterNodeStackStateSaver(prev);

	for(size_t i = 1; i < ocn.size(); i++)
	{
		//if not in strict increasing order, return false
		auto cur = InterpretNodeForImmediateUse(ocn[i]);

		if(EvaluableNode::IsEmptyNode(cur))
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(prev);
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		if(!EvaluableNode::IsLessThan(cur, prev, en->GetType() == ENT_GEQUAL))
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(prev);
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		evaluableNodeManager->FreeNodeTreeIfPossible(prev);
		prev = cur;

		node_stack.PopEvaluableNode();
		node_stack.PushEvaluableNode(prev);
	}

	evaluableNodeManager->FreeNodeTreeIfPossible(prev);

	//nothing is out of order
	return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_TRUE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_TYPE_EQUALS(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	bool processed_first_value = false;
	EvaluableNodeReference to_match = EvaluableNodeReference::Null();

#ifdef MULTITHREAD_SUPPORT
	std::vector<EvaluableNodeReference> interpreted_nodes;
	if(InterpretEvaluableNodesConcurrently(en, ocn, interpreted_nodes))
	{
		for(auto &cur : interpreted_nodes)
		{
			//if haven't gotten a value yet, then use this as the first data
			if(!processed_first_value)
			{
				to_match = cur;
				processed_first_value = true;
				continue;
			}

			EvaluableNodeType cur_type = ENT_NULL;
			if(cur != nullptr)
				cur_type = cur->GetType();

			EvaluableNodeType to_match_type = ENT_NULL;
			if(to_match != nullptr)
				to_match_type = to_match->GetType();

			if(cur_type != to_match_type)
			{
				evaluableNodeManager->FreeNodeTreeIfPossible(to_match);
				evaluableNodeManager->FreeNodeTreeIfPossible(cur);

				return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
			}

			evaluableNodeManager->FreeNodeTreeIfPossible(cur);
		}

		evaluableNodeManager->FreeNodeTreeIfPossible(to_match);

		return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_TRUE), true);
	}
#endif

	auto node_stack = CreateInterpreterNodeStackStateSaver();

	for(auto &cn : ocn)
	{
		auto cur = InterpretNodeForImmediateUse(cn);

		//if haven't gotten a value yet, then use this as the first data
		if(!processed_first_value)
		{
			to_match = cur;
			node_stack.PushEvaluableNode(to_match);
			processed_first_value = true;
			continue;
		}

		EvaluableNodeType cur_type = ENT_NULL;
		if(cur != nullptr)
			cur_type = cur->GetType();
		
		EvaluableNodeType to_match_type = ENT_NULL;
		if(to_match != nullptr)
			to_match_type = to_match->GetType();

		if(cur_type != to_match_type)
		{
			evaluableNodeManager->FreeNodeTreeIfPossible(to_match);
			evaluableNodeManager->FreeNodeTreeIfPossible(cur);

			return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_FALSE), true);
		}

		evaluableNodeManager->FreeNodeTreeIfPossible(cur);
	}

	evaluableNodeManager->FreeNodeTreeIfPossible(to_match);

	return EvaluableNodeReference(evaluableNodeManager->AllocNode(ENT_TRUE), true);
}

EvaluableNodeReference Interpreter::InterpretNode_ENT_TYPE_NEQUALS(EvaluableNode *en)
{
	auto &ocn = en->GetOrderedChildNodes();

	if(ocn.size() == 0)
		return EvaluableNodeReference::Null();

	EvaluableNodeType result_type = ENT_TRUE;

	std::vector<EvaluableNode *> values(ocn.size());

	auto node_stack = CreateInterpreterNodeStackStateSaver();

	//evaluate all nodes just once
	for(size_t i = 0; i < ocn.size(); i++)
	{
		values[i] = InterpretNodeForImmediateUse(ocn[i]);
		node_stack.PushEvaluableNode(values[i]);
	}

	for(size_t i = 0; i < ocn.size(); i++)
	{
		//start at next higher, because comparisons are symmetric and don't need to compare with self
		for(size_t j = i + 1; j < ocn.size(); j++)
		{
			EvaluableNode *cur1 = values[i];
			EvaluableNode *cur2 = values[j];

			//if they're equal, then it fails
			if((cur1 == nullptr && cur2 == nullptr) || (cur1 != nullptr && cur2 != nullptr && cur1->GetType() == cur2->GetType()))
			{
				result_type = ENT_FALSE;

				//break out of loop
				i = ocn.size();
				break;
			}
		}
	}

	return EvaluableNodeReference(evaluableNodeManager->AllocNode(result_type), true);
}
