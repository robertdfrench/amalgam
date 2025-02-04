#pragma once

//project headers:
#include "EntityQueries.h"
#include "EvaluableNode.h"
#include "StringInternPool.h"

//Constructs a query engine query condition from Amalgam evaluable nodes
namespace EntityQueryBuilder
{
	//parameter indices for distance queries
	enum DistParamIndices : size_t
	{
		MAX_TO_FIND_OR_MAX_DISTANCE,
		POSITION_LABELS,
		POSITION,

		WEIGHTS,
		DISTANCE_TYPES,
		ATTRIBUTES,
		DEVIATIONS,

		//optional params
		MINKOWSKI_PARAMETER,
		DISTANCE_VALUE_TRANSFORM,
		ENTITY_WEIGHT_LABEL_NAME,
		RANDOM_SEED,
		RADIUS_LABEL,
		NUMERICAL_PRECISION,
		
		NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS //always last - do not add after this
	};

	constexpr bool DoesDistanceQueryUseEntitiesInsteadOfPosition(EvaluableNodeType type)
	{
		return (type == ENT_COMPUTE_ENTITY_CONVICTIONS
			|| type == ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE
			|| type == ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS
			|| type == ENT_COMPUTE_ENTITY_KL_DIVERGENCES);
	}

	//populates the features of dist_params based on either num_elements or element_names for each of the
	// four different attribute parameters based on its type (using num_elements if list or immediate, element_names if assoc)
	inline void PopulateDistanceFeatureParameters(GeneralizedDistance &dist_params,
		size_t num_elements, std::vector<StringInternPool::StringID> &element_names,
		EvaluableNode *weights_node, EvaluableNode *distance_types_node, EvaluableNode *attributes_node, EvaluableNode *deviations_node)
	{
		dist_params.featureParams.resize(num_elements);

		//get weights
		EvaluableNode::ConvertChildNodesAndStoreValue(weights_node, element_names, num_elements,
			[&dist_params](size_t i, bool found, EvaluableNode *en) {
				if(i < dist_params.featureParams.size())
				{
					if(found)
						dist_params.featureParams[i].weight = EvaluableNode::ToNumber(en);
					else
						dist_params.featureParams[i].weight = 1.0;
				}
			});

		//get type
		EvaluableNode::ConvertChildNodesAndStoreValue(distance_types_node, element_names, num_elements,
			[&dist_params](size_t i, bool found, EvaluableNode *en) {
				if(i < dist_params.featureParams.size())
				{
					auto feature_type = FDT_CONTINUOUS_NUMERIC;
					if(found)
					{
						StringInternPool::StringID feature_type_id = EvaluableNode::ToStringIDIfExists(en);
						switch(feature_type_id)
						{
						case ENBISI_nominal:								feature_type = FDT_NOMINAL;						break;
						case ENBISI_continuous:								feature_type = FDT_CONTINUOUS_NUMERIC;			break;
						case ENBISI_cyclic:									feature_type = FDT_CONTINUOUS_NUMERIC_CYCLIC;	break;
						case GetStringIdFromNodeTypeFromString(ENT_STRING): feature_type = FDT_CONTINUOUS_STRING;			break;	
						case ENBISI_code:									feature_type = FDT_CONTINUOUS_CODE;				break;
						default:											feature_type = FDT_CONTINUOUS_NUMERIC;			break;
						}
					}
					dist_params.featureParams[i].featureType = feature_type;
				}
			});

		//get attributes
		EvaluableNode::ConvertChildNodesAndStoreValue(attributes_node, element_names, num_elements,
			[&dist_params](size_t i, bool found, EvaluableNode *en) {
				if(i < dist_params.featureParams.size())
				{
					dist_params.featureParams[i].unknownToUnknownDifference = std::numeric_limits<double>::quiet_NaN();
					dist_params.featureParams[i].knownToUnknownDifference = std::numeric_limits<double>::quiet_NaN();

					//get attributes based on feature type
					switch(dist_params.featureParams[i].featureType)
					{
					case FDT_NOMINAL:
						if(found && !EvaluableNode::IsNull(en))
						{
							if(en->EvaluableNode::IsOrderedArray())
							{
								auto &ocn = en->GetOrderedChildNodesReference();
								size_t ocn_size = ocn.size();
								if(ocn_size > 0)
									dist_params.featureParams[i].typeAttributes.nominalCount = EvaluableNode::ToNumber(ocn[0]);
								if(ocn_size > 1)
									dist_params.featureParams[i].knownToUnknownDifference = EvaluableNode::ToNumber(ocn[1]);
								if(ocn_size > 2)
									dist_params.featureParams[i].unknownToUnknownDifference = EvaluableNode::ToNumber(ocn[2]);
							}
							else //treat as singular value
							{
								dist_params.featureParams[i].typeAttributes.nominalCount = EvaluableNode::ToNumber(en);
							}
						}
						else
						{
							dist_params.featureParams[i].typeAttributes.nominalCount = 0.0;
						}
						break;

					case FDT_CONTINUOUS_NUMERIC_CYCLIC:
						if(found && !EvaluableNode::IsNull(en))
						{
							if(en->EvaluableNode::IsOrderedArray())
							{
								auto &ocn = en->GetOrderedChildNodesReference();
								size_t ocn_size = ocn.size();
								if(ocn_size > 0)
									dist_params.featureParams[i].typeAttributes.maxCyclicDifference = EvaluableNode::ToNumber(ocn[0]);
								if(ocn_size > 1)
									dist_params.featureParams[i].knownToUnknownDifference = EvaluableNode::ToNumber(ocn[1]);
								if(ocn_size > 2)
									dist_params.featureParams[i].unknownToUnknownDifference = EvaluableNode::ToNumber(ocn[2]);
							}
							else //treat as singular value
							{
								dist_params.featureParams[i].typeAttributes.maxCyclicDifference = EvaluableNode::ToNumber(en);
							}
						}
						else //can't be cyclic without a range
						{
							dist_params.featureParams[i].featureType = FDT_CONTINUOUS_NUMERIC;
						}
						break;

					case FDT_CONTINUOUS_NUMERIC:
					case FDT_CONTINUOUS_UNIVERSALLY_NUMERIC:
					case FDT_CONTINUOUS_STRING:
					case FDT_CONTINUOUS_CODE:
						if(found && !EvaluableNode::IsNull(en))
						{
							if(en->EvaluableNode::IsOrderedArray())
							{
								auto &ocn = en->GetOrderedChildNodesReference();
								size_t ocn_size = ocn.size();
								if(ocn_size > 0)
									dist_params.featureParams[i].knownToUnknownDifference = EvaluableNode::ToNumber(ocn[0]);
								if(ocn_size > 1)
									dist_params.featureParams[i].unknownToUnknownDifference = EvaluableNode::ToNumber(ocn[1]);
							}
							else //treat as singular value
							{
								dist_params.featureParams[i].knownToUnknownDifference = EvaluableNode::ToNumber(en);
							}
						}
						break;
					}
				}
			});

		//get deviations
		EvaluableNode::ConvertChildNodesAndStoreValue(deviations_node, element_names, num_elements,
			[&dist_params](size_t i, bool found, EvaluableNode *en) {
				if(i < dist_params.featureParams.size())
				{
					if(found)
						dist_params.featureParams[i].deviation = EvaluableNode::ToNumber(en);
					else
						dist_params.featureParams[i].deviation = 0.0;
				}
			});
	}


	//interpret evaluable node as a distance query
	inline void BuildDistanceCondition(EvaluableNode *cn, EvaluableNodeType condition_type, std::vector<EntityQueryCondition> &conditions)
	{
		//cache ordered child nodes so don't need to keep fetching
		auto &ocn = cn->GetOrderedChildNodes();

		//need to at least have position, otherwise not valid query
		if(ocn.size() <= POSITION)
			return;

		//if ENT_QUERY_NEAREST_GENERALIZED_DISTANCE, see if excluding an entity in the previous query -- if so, exclude here
		EntityQueryCondition *cur_condition = nullptr;
		if(condition_type == ENT_QUERY_NEAREST_GENERALIZED_DISTANCE && conditions.size() > 0
			&& conditions.back().queryType == ENT_QUERY_NOT_IN_ENTITY_LIST && conditions.back().existLabels.size() == 1)
		{
			cur_condition = &(conditions.back());
			cur_condition->exclusionLabel = cur_condition->existLabels[0];
			cur_condition->existLabels.clear();
		}
		else
		{
			//create a new condition for distance
			conditions.emplace_back();
			cur_condition = &(conditions.back());

			cur_condition->exclusionLabel = string_intern_pool.NOT_A_STRING_ID;
		}

		//set query condition type
		cur_condition->queryType = condition_type;
		cur_condition->useConcurrency = cn->GetConcurrency();

		//set maximum distance and max number of results (top_k) to find
		if(condition_type == ENT_QUERY_WITHIN_GENERALIZED_DISTANCE) //maximum distance to search within
		{
			cur_condition->maxToRetrieve = std::numeric_limits<double>::infinity();
			cur_condition->maxDistance = EvaluableNode::ToNumber(ocn[MAX_TO_FIND_OR_MAX_DISTANCE]);
			if(FastIsNaN(cur_condition->maxDistance))
				cur_condition->maxDistance = 0;
		}
		else //infinite range query, use param as number to find (top_k)
		{
			cur_condition->maxToRetrieve = EvaluableNode::ToNumber(ocn[MAX_TO_FIND_OR_MAX_DISTANCE]);
			if(FastIsNaN(cur_condition->maxToRetrieve))
				cur_condition->maxToRetrieve = 0;
			cur_condition->maxDistance = std::numeric_limits<double>::infinity();
		}

		//set position labels
		EvaluableNode *position_labels = ocn[POSITION_LABELS];
		if(EvaluableNode::IsOrderedArray(position_labels))
		{
			cur_condition->positionLabels.reserve(position_labels->GetOrderedChildNodes().size());
			for(auto &pl : position_labels->GetOrderedChildNodes())
			{
				StringInternPool::StringID label_sid = EvaluableNode::ToStringIDIfExists(pl);
				if(Entity::IsLabelValidAndPublic(label_sid))
					cur_condition->positionLabels.push_back(label_sid);
				else
					cur_condition->queryType = ENT_NULL;
			}
		}

		//select based on type for position or entities
		if(DoesDistanceQueryUseEntitiesInsteadOfPosition(condition_type))
		{
			EvaluableNode *entities = ocn[POSITION];
			if(EvaluableNode::IsOrderedArray(entities))
			{
				auto &entities_ocn = entities->GetOrderedChildNodesReference();
				cur_condition->existLabels.reserve(entities_ocn.size());
				for(auto &entity_en : entities_ocn)
					cur_condition->existLabels.push_back(EvaluableNode::ToStringIDIfExists(entity_en));
			}
		}
		else
		{
			//set position
			EvaluableNode *position = ocn[POSITION];
			if(EvaluableNode::IsOrderedArray(position) && (position->GetNumChildNodes() == cur_condition->positionLabels.size()))
			{
				auto &position_ocn = position->GetOrderedChildNodesReference();
				cur_condition->valueToCompare.reserve(position_ocn.size());
				cur_condition->valueTypes.reserve(position_ocn.size());
				for(auto &pos_en : position_ocn)
				{
					EvaluableNodeImmediateValue imm_val;
					auto value_type = imm_val.CopyValueFromEvaluableNode(pos_en);
					cur_condition->valueTypes.push_back(value_type);
					cur_condition->valueToCompare.push_back(imm_val);
				}
			}
			else // no positions given, default to nulls for each label
			{
				cur_condition->valueToCompare.reserve(cur_condition->positionLabels.size());
				cur_condition->valueTypes.reserve(cur_condition->positionLabels.size());
				for(size_t i = 0; i < cur_condition->positionLabels.size(); i++)
				{
					cur_condition->valueTypes.push_back(ENIVT_NULL);
					cur_condition->valueToCompare.push_back(EvaluableNodeImmediateValue());
				}
			}
		}
		//else don't bother parsing this, it instead contains the cases to compute case conviction for

		size_t num_elements = cur_condition->positionLabels.size();
		auto &dist_params = cur_condition->distParams;

		EvaluableNode *weights_node = nullptr;
		if(ocn.size() > WEIGHTS)
			weights_node = ocn[WEIGHTS];

		EvaluableNode *distance_types_node = nullptr;
		if(ocn.size() > DISTANCE_TYPES)
			distance_types_node = ocn[DISTANCE_TYPES];

		EvaluableNode *attributes_node = nullptr;
		if(ocn.size() > ATTRIBUTES)
			attributes_node = ocn[ATTRIBUTES];

		EvaluableNode *deviations_node = nullptr;
		if(ocn.size() > DEVIATIONS)
			deviations_node = ocn[DEVIATIONS];

		PopulateDistanceFeatureParameters(dist_params, num_elements, cur_condition->positionLabels,
			weights_node, distance_types_node, attributes_node, deviations_node);

		//set minkowski parameter; default to 2.0 for Euclidian distance
		double p_value = 2.0;
		if(ocn.size() > MINKOWSKI_PARAMETER)
		{
			p_value = EvaluableNode::ToNumber(ocn[MINKOWSKI_PARAMETER]);

			//make sure valid value, if not, fall back to 2
			if(FastIsNaN(p_value) || p_value < 0)
				p_value = 2;
		}
		cur_condition->distParams.pValue = p_value;

		//value transforms for whatever is measured as "distance"
		cur_condition->transformSuprisalToProb = false;
		cur_condition->distanceWeightExponent = 1.0;
		if(ocn.size() > DISTANCE_VALUE_TRANSFORM)
		{
			EvaluableNode *dwe_param = ocn[DISTANCE_VALUE_TRANSFORM];
			if(!EvaluableNode::IsNull(dwe_param))
			{
				if(dwe_param->GetType() == ENT_STRING && dwe_param->GetStringIDReference() == ENBISI_surprisal_to_prob)
					cur_condition->transformSuprisalToProb = true;
				else //try to convert to number
					cur_condition->distanceWeightExponent = EvaluableNode::ToNumber(dwe_param, 1.0);
			}
		}

		cur_condition->weightLabel = StringInternPool::NOT_A_STRING_ID;
		if(ocn.size() > ENTITY_WEIGHT_LABEL_NAME)
			cur_condition->weightLabel = EvaluableNode::ToStringIDIfExists(ocn[ENTITY_WEIGHT_LABEL_NAME]);

		//set random seed
		std::string seed = "";
		if(ocn.size() > RANDOM_SEED)
			seed = EvaluableNode::ToString(ocn[RANDOM_SEED]);
		cur_condition->randomStream.SetState(seed);

		//set radius label
		if(ocn.size() > RADIUS_LABEL)
			cur_condition->singleLabel = EvaluableNode::ToStringIDIfExists(ocn[RADIUS_LABEL]);
		else
			cur_condition->singleLabel = StringInternPool::NOT_A_STRING_ID;

		//set numerical precision
		cur_condition->distParams.highAccuracy = false;
		cur_condition->distParams.recomputeAccurateDistances = true;
		if(ocn.size() > NUMERICAL_PRECISION)
		{
			StringInternPool::StringID np_sid = EvaluableNode::ToStringIDIfExists(ocn[NUMERICAL_PRECISION]);
			if(np_sid == ENBISI_precise)
			{
				cur_condition->distParams.highAccuracy = true;
				cur_condition->distParams.recomputeAccurateDistances = false;
			}
			else if(np_sid == ENBISI_fast)
			{
				cur_condition->distParams.highAccuracy = false;
				cur_condition->distParams.recomputeAccurateDistances = false;
			}
			//don't need to do anything for np_sid == ENBISI_recompute_precise because it's default
		}
		
		cur_condition->returnSortedList = false;
		cur_condition->additionalSortedListLabel = string_intern_pool.NOT_A_STRING_ID;
		if(condition_type == ENT_QUERY_WITHIN_GENERALIZED_DISTANCE || condition_type == ENT_QUERY_NEAREST_GENERALIZED_DISTANCE || condition_type == ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS)
		{
			if(ocn.size() > NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS + 0)
			{
				EvaluableNode *list_param = ocn[NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS + 0];
				cur_condition->returnSortedList = EvaluableNode::IsTrue(list_param);
				if(!EvaluableNode::IsEmptyNode(list_param) && list_param->GetType() != ENT_TRUE && list_param->GetType() != ENT_FALSE)
					cur_condition->additionalSortedListLabel = EvaluableNode::ToStringIDIfExists(list_param);
			}
		}
		else if(condition_type == ENT_COMPUTE_ENTITY_CONVICTIONS || condition_type == ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE || condition_type == ENT_COMPUTE_ENTITY_KL_DIVERGENCES)
		{
			cur_condition->convictionOfRemoval = false;
			if(ocn.size() > NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS + 0)
				cur_condition->convictionOfRemoval = EvaluableNode::IsTrue(ocn[NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS + 0]);

			if(condition_type == ENT_COMPUTE_ENTITY_CONVICTIONS || condition_type == ENT_COMPUTE_ENTITY_KL_DIVERGENCES)
			{
				if(ocn.size() > NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS + 1)
				{
					EvaluableNode *list_param = ocn[NUM_MINKOWSKI_DISTANCE_QUERY_PARAMETERS + 1];
					cur_condition->returnSortedList = EvaluableNode::IsTrue(list_param);
					if(!EvaluableNode::IsEmptyNode(list_param) && list_param->GetType() != ENT_TRUE && list_param->GetType() != ENT_FALSE)
						cur_condition->additionalSortedListLabel = EvaluableNode::ToStringIDIfExists(list_param);
				}
			}
		}
		

		//check if any of the positions are not valid
		bool need_exist_query = false;
		bool has_position_data = !DoesDistanceQueryUseEntitiesInsteadOfPosition(condition_type);

		//check for any disabled features (e.g., zero'd weight)
		if(has_position_data)
		{
			for(size_t i = 0; i < cur_condition->distParams.featureParams.size(); i++)
			{
				if(!cur_condition->distParams.IsFeatureEnabled(i))
				{
					need_exist_query = true;
					break;
				}
			}
		}
		else //entities may have missing data, so need exist query
		{
			need_exist_query = true;
		}

		if(need_exist_query)
		{
			//add exists query and swap, so the exists_condition is before cur_condition
			conditions.emplace_back();
			EntityQueryCondition *exists_condition = &(conditions.back());

			//need to reretrieve the pointer in case there has been a reallocation via emplace_back
			// don't get the end one just placed, get the one before that
			cur_condition = &conditions[conditions.size() - 2];

			//swap data and pointers
			std::swap(*exists_condition, *cur_condition);
			std::swap(exists_condition, cur_condition);

			exists_condition->queryType = ENT_QUERY_EXISTS;
			//if has_position_data, then will add on those needed features below
			// but if it doesn't, then need to include all labels
			if(!has_position_data)
				exists_condition->existLabels = cur_condition->positionLabels;

			//remove any 0 weighted features; if has_position_data, then move them to the exist query
			// don't increment i here because if a feature is moved to the exists_condition,
			// then a new feature is moved into that new index and that feature position needs to be rechecked
			for(size_t i = 0; i < cur_condition->positionLabels.size();)
			{
				if(cur_condition->distParams.featureParams[i].weight == 0.0)
				{
					//only move/remove data if the right type of query
					if(has_position_data)
					{
						exists_condition->existLabels.push_back(cur_condition->positionLabels[i]);
						cur_condition->valueToCompare.erase(cur_condition->valueToCompare.begin() + i);
						cur_condition->valueTypes.erase(cur_condition->valueTypes.begin() + i);
					}

					cur_condition->positionLabels.erase(cur_condition->positionLabels.begin() + i);
					cur_condition->distParams.featureParams.erase(begin(cur_condition->distParams.featureParams) + i);
					continue;
				}

				i++;
			}
		}

		//perform this last to make sure all changes are in
		cur_condition->distParams.SetAndConstrainParams();
	}

	//builds a query condition from cn
	inline void BuildNonDistanceCondition(EvaluableNode *cn, EvaluableNodeType type,
		std::vector<EntityQueryCondition> &conditions, EvaluableNodeManager &enm, RandomStream &rs)
	{
		auto &ocn = cn->GetOrderedChildNodes();

		//validate number of parameters
		switch(type)
		{
			case ENT_QUERY_BETWEEN: //all double parameter query types
			case ENT_QUERY_NOT_BETWEEN:
				if(ocn.size() < 3)
					return;
				break;

			case ENT_QUERY_LESS_OR_EQUAL_TO:
			case ENT_QUERY_GREATER_OR_EQUAL_TO:
			case ENT_QUERY_NOT_EQUALS:
			case ENT_QUERY_EQUALS:
				if(ocn.size() < 2)
					return;
				break;

			case ENT_QUERY_MIN:
			case ENT_QUERY_MAX:
			case ENT_QUERY_VALUE_MASSES:
				if(ocn.size() < 1)
					return;
				break;

			default:;
		}

		//next, determine if a a new condition should be made, or reuse the current one
		bool requires_new_condition = true; //if true, create a new condition rather than using current_condition
		switch(type)
		{
			case ENT_QUERY_NOT_EXISTS:
			case ENT_QUERY_EXISTS:
			case ENT_QUERY_NOT_EQUALS:
			case ENT_QUERY_EQUALS:
			case ENT_QUERY_NOT_BETWEEN:
				requires_new_condition = (conditions.size() == 0 || conditions.back().queryType != type);
				break;

			case ENT_QUERY_BETWEEN:
			case ENT_QUERY_GREATER_OR_EQUAL_TO:
			case ENT_QUERY_LESS_OR_EQUAL_TO:
			{
				//these three are equivalent
				if(conditions.size() > 0)
				{
					EvaluableNodeType prev_type = conditions.back().queryType;
					if(prev_type == ENT_QUERY_BETWEEN || prev_type == ENT_QUERY_GREATER_OR_EQUAL_TO || prev_type == ENT_QUERY_LESS_OR_EQUAL_TO)
						requires_new_condition = false;
				}
				break;
			}

			default:;
		}

		//create a new condition if needed
		if(requires_new_condition)
		{
			//create new condition
			conditions.emplace_back();
			conditions.back().queryType = type;
		}

		auto cur_condition = &(conditions.back());
		cur_condition->singleLabel = 0;

		//get label sid - always the first child node
		StringInternPool::StringID label_sid = (ocn.size() >= 1) ? EvaluableNode::ToStringIDIfExists(ocn[0]) : StringInternPool::NOT_A_STRING_ID;

		//switch to return if label sid is invalid
		switch(type)
		{
			case ENT_QUERY_NOT_EXISTS:
			case ENT_QUERY_EXISTS:
			case ENT_QUERY_MIN:
			case ENT_QUERY_MAX:
			case ENT_QUERY_SUM:
			case ENT_QUERY_MODE:
			case ENT_QUERY_QUANTILE:
			case ENT_QUERY_GENERALIZED_MEAN:
			case ENT_QUERY_MIN_DIFFERENCE:
			case ENT_QUERY_MAX_DIFFERENCE:
			case ENT_QUERY_VALUE_MASSES:
			case ENT_QUERY_LESS_OR_EQUAL_TO:
			case ENT_QUERY_GREATER_OR_EQUAL_TO:
			case ENT_QUERY_NOT_EQUALS:
			case ENT_QUERY_EQUALS:
			case ENT_QUERY_BETWEEN:
			case ENT_QUERY_NOT_BETWEEN:
			case ENT_QUERY_AMONG:
			case ENT_QUERY_NOT_AMONG:
			{
				if(!Entity::IsLabelValidAndPublic(label_sid))
				{
					cur_condition->queryType = ENT_NULL;
					return;
				}
				break;
			}
			default:;
		}

		//actually populate the condition parameters from the evaluable nodes
		switch(type)
		{
			case ENT_QUERY_SELECT:
			{
				cur_condition->maxToRetrieve = (ocn.size() >= 1) ? EvaluableNode::ToNumber(ocn[0], 0.0) : 0;

				cur_condition->hasStartOffset = (ocn.size() >= 2);
				cur_condition->startOffset = cur_condition->hasStartOffset ? static_cast<size_t>(EvaluableNode::ToNumber(ocn[1], 0.0)) : 0;

				cur_condition->hasRandomStream = (ocn.size() >= 3 && !EvaluableNode::IsEmptyNode(ocn[2]));
				if(cur_condition->hasRandomStream)
					cur_condition->randomStream.SetState(EvaluableNode::ToString(ocn[2]));

				break;
			}
			case ENT_QUERY_SAMPLE:
			{
				cur_condition->maxToRetrieve = (ocn.size() > 0) ? EvaluableNode::ToNumber(ocn[0], 0.0) : 1;
				cur_condition->hasRandomStream = (ocn.size() > 1 && !EvaluableNode::IsEmptyNode(ocn[1]));
				if(cur_condition->hasRandomStream)
					cur_condition->randomStream.SetState(EvaluableNode::ToString(ocn[1]));
				else
					cur_condition->randomStream = rs.CreateOtherStreamViaRand();
			    break;
			}
			case ENT_QUERY_WEIGHTED_SAMPLE:
			{
				cur_condition->singleLabel = (ocn.size() > 0) ? EvaluableNode::ToStringIDIfExists(ocn[0]) : StringInternPool::NOT_A_STRING_ID;
				cur_condition->maxToRetrieve = (ocn.size() > 1) ? EvaluableNode::ToNumber(ocn[1], 0.0) : 1;
				cur_condition->hasRandomStream = (ocn.size() > 2 && !EvaluableNode::IsEmptyNode(ocn[2]));
				if(cur_condition->hasRandomStream)
					cur_condition->randomStream.SetState(EvaluableNode::ToString(ocn[2]));
				else
					cur_condition->randomStream = rs.CreateOtherStreamViaRand();
				break;
			}
			case ENT_QUERY_IN_ENTITY_LIST:
			case ENT_QUERY_NOT_IN_ENTITY_LIST:
			{
				if(ocn.size() >= 1)
				{
					EvaluableNode *entity_sids = ocn[0];
					if(EvaluableNode::IsOrderedArray(entity_sids))
					{
						cur_condition->existLabels.reserve(entity_sids->GetOrderedChildNodes().size());
						for(auto &esid : entity_sids->GetOrderedChildNodes())
						{
							StringInternPool::StringID entity_sid = EvaluableNode::ToStringIDIfExists(esid);
							cur_condition->existLabels.push_back(entity_sid);
						}
					}
				}
				break;
			}
			case ENT_QUERY_BETWEEN:
			case ENT_QUERY_NOT_BETWEEN:
			{
				//number of parameters checked above
				EvaluableNode *low_value = ocn[1];
				EvaluableNode *high_value = ocn[2];

				//since types need to match, force both to the same type
				if(EvaluableNode::IsNativelyNumeric(low_value) || EvaluableNode::IsNativelyNumeric(high_value))
				{
					cur_condition->pairedLabels.push_back(std::make_pair(label_sid, std::make_pair(
						EvaluableNode::ToNumber(low_value), EvaluableNode::ToNumber(high_value))));

					cur_condition->valueTypes.push_back(ENIVT_NUMBER);
				}
				else
				{
					StringInternPool::StringID low_sid = EvaluableNode::ToStringIDIfExists(low_value);
					StringInternPool::StringID high_sid = EvaluableNode::ToStringIDIfExists(high_value);

					cur_condition->pairedLabels.push_back(std::make_pair(label_sid, std::make_pair(low_sid, high_sid)));

					cur_condition->valueTypes.push_back(ENIVT_STRING_ID);
				}

				break;
			}

			case ENT_QUERY_AMONG:
			case ENT_QUERY_NOT_AMONG:
			{
				cur_condition->singleLabel = label_sid;

				//already checked for nullptr above
				auto &values_ocn = ocn[1]->GetOrderedChildNodes();
				for(auto value_node : values_ocn)
				{
					EvaluableNodeImmediateValue value;
					auto value_type = value.CopyValueFromEvaluableNode(value_node);
					cur_condition->valueToCompare.push_back(value);
					cur_condition->valueTypes.push_back(value_type);
				}

				break;
			}

			case ENT_QUERY_NOT_EXISTS:
			case ENT_QUERY_EXISTS:
			{
				//get label and append it if it is valid (otherwise don't match on anything)
				if(ocn.size() >= 1)
					cur_condition->existLabels.push_back(label_sid);

				break;
			}

			case ENT_QUERY_MIN:
			case ENT_QUERY_MAX:
			{
				cur_condition->singleLabel = label_sid;

				//default to retrieve 1
				cur_condition->maxToRetrieve = 1;
				if(ocn.size() >= 2)
				{
					EvaluableNode *value = ocn[1];
					cur_condition->maxToRetrieve = EvaluableNode::ToNumber(value);
				}

				if(ocn.size() <= 2 || EvaluableNode::IsTrue(ocn[2]))
					cur_condition->singleLabelType = ENIVT_NUMBER;
				else
					cur_condition->singleLabelType = ENIVT_STRING_ID;

				break;
			}

			case ENT_QUERY_LESS_OR_EQUAL_TO:
			case ENT_QUERY_GREATER_OR_EQUAL_TO:
			{
				//these query types will be transformed into a between query, including the appropriate infinite

				//number of parameters checked above
				EvaluableNode *compare_value = ocn[1];

				if(EvaluableNode::IsNativelyNumeric(compare_value))
				{
					if(type == ENT_QUERY_LESS_OR_EQUAL_TO)
						cur_condition->pairedLabels.push_back(std::make_pair(label_sid, std::make_pair(
							-std::numeric_limits<double>::infinity(), EvaluableNode::ToNumber(compare_value))));
					else
						cur_condition->pairedLabels.push_back(std::make_pair(label_sid, std::make_pair(
							EvaluableNode::ToNumber(compare_value), std::numeric_limits<double>::infinity())));

					cur_condition->valueTypes.push_back(ENIVT_NUMBER);
				}
				else
				{
					if(type == ENT_QUERY_LESS_OR_EQUAL_TO)
						cur_condition->pairedLabels.push_back(std::make_pair(label_sid, std::make_pair(
							string_intern_pool.NOT_A_STRING_ID, EvaluableNode::ToStringIDIfExists(compare_value))));
					else
						cur_condition->pairedLabels.push_back(std::make_pair(label_sid, std::make_pair(
							EvaluableNode::ToStringIDIfExists(compare_value), string_intern_pool.NOT_A_STRING_ID)));

					cur_condition->valueTypes.push_back(ENIVT_STRING_ID);
				}

				cur_condition->queryType = ENT_QUERY_BETWEEN;
				break;
			}


			case ENT_QUERY_NOT_EQUALS:
			case ENT_QUERY_EQUALS:
			{				
				EvaluableNodeImmediateValue value;
				EvaluableNodeImmediateValueType value_type = value.CopyValueFromEvaluableNode(ocn[1]);

				cur_condition->valueTypes.push_back(value_type);
				cur_condition->singleLabels.emplace_back(std::make_pair(label_sid, value));

				break;
			}

			case ENT_QUERY_MIN_DIFFERENCE:
				cur_condition->singleLabel = label_sid;

				cur_condition->maxDistance = std::numeric_limits<double>::quiet_NaN();
				if(ocn.size() >= 2)
					cur_condition->maxDistance = EvaluableNode::ToNumber(ocn[1]);

				cur_condition->includeZeroDifferences = true;
				if(ocn.size() >= 3)
					cur_condition->includeZeroDifferences = EvaluableNode::IsTrue(ocn[2]);
				break;

			case ENT_QUERY_MAX_DIFFERENCE:
				cur_condition->singleLabel = label_sid;
				
				cur_condition->maxDistance = std::numeric_limits<double>::quiet_NaN();
				if(ocn.size() >= 2)
					cur_condition->maxDistance = EvaluableNode::ToNumber(ocn[1]);

				break;

			case ENT_QUERY_SUM:
			case ENT_QUERY_MODE:
			case ENT_QUERY_VALUE_MASSES:
			{
				cur_condition->singleLabel = label_sid;

				cur_condition->weightLabel = StringInternPool::NOT_A_STRING_ID;
				if(ocn.size() >= 2)
					cur_condition->weightLabel = EvaluableNode::ToStringIDIfExists(ocn[1]);

				if(type == ENT_QUERY_MODE || type == ENT_QUERY_VALUE_MASSES)
				{
					if(ocn.size() <= 2 || EvaluableNode::IsTrue(ocn[2]))
						cur_condition->singleLabelType = ENIVT_NUMBER;
					else
						cur_condition->singleLabelType = ENIVT_STRING_ID;
				}

				break;
			}

			case ENT_QUERY_QUANTILE:
			{
				cur_condition->singleLabel = label_sid;

				cur_condition->qPercentage = 0.5;
				if(ocn.size() >= 2)
					cur_condition->qPercentage = EvaluableNode::ToNumber(ocn[1]);

				cur_condition->weightLabel = StringInternPool::NOT_A_STRING_ID;
				if(ocn.size() >= 3)
					cur_condition->weightLabel = EvaluableNode::ToStringIDIfExists(ocn[2]);

				break;
			}

			case ENT_QUERY_GENERALIZED_MEAN:
			{
				cur_condition->singleLabel = label_sid;

				cur_condition->distParams.pValue = 1;
				if(ocn.size() >= 2)
					cur_condition->distParams.pValue = EvaluableNode::ToNumber(ocn[1]);

				cur_condition->weightLabel = StringInternPool::NOT_A_STRING_ID;
				if(ocn.size() >= 3)
					cur_condition->weightLabel = EvaluableNode::ToStringIDIfExists(ocn[2]);

				cur_condition->center = 0.0;
				if(ocn.size() >= 4)
					cur_condition->center = EvaluableNode::ToNumber(ocn[3], 0.0);

				cur_condition->calculateMoment = false;
				if(ocn.size() >= 5)
					cur_condition->calculateMoment = EvaluableNode::IsTrue(ocn[4]);

				cur_condition->absoluteValue = false;
				if(ocn.size() >= 6)
					cur_condition->absoluteValue = EvaluableNode::IsTrue(ocn[5]);

				break;
			}

			default:;
		}//end switch
	}
};
