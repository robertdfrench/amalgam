//project headers:
#include "Conviction.h"
#include "EntityQueries.h"
#include "EntityQueryCaches.h"

#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
thread_local
#endif
EntityQueryCaches::QueryCachesBuffers EntityQueryCaches::buffers;

#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
void EntityQueryCaches::EnsureLabelsAreCached(EntityQueryCondition *cond, Concurrency::ReadLock &lock)
#else
void EntityQueryCaches::EnsureLabelsAreCached(EntityQueryCondition *cond)
#endif
{
	//if there are any labels that need to be added,
	// this will collected them to be added all at once
	std::vector<StringInternPool::StringID> labels_to_add;

	//add label to cache if missing
	switch(cond->queryType)
	{
		case ENT_QUERY_NEAREST_GENERALIZED_DISTANCE:
		case ENT_QUERY_WITHIN_GENERALIZED_DISTANCE:
		case ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS:
		case ENT_COMPUTE_ENTITY_CONVICTIONS:
		case ENT_COMPUTE_ENTITY_KL_DIVERGENCES:
		case ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE:
		{
			for(auto label : cond->positionLabels)
			{
				if(!DoesHaveLabel(label))
					labels_to_add.push_back(label);
			}

			if(cond->weightLabel != StringInternPool::NOT_A_STRING_ID)
			{
				if(!DoesHaveLabel(cond->weightLabel))
					labels_to_add.push_back(cond->weightLabel);
			}

			if(cond->additionalSortedListLabel != StringInternPool::NOT_A_STRING_ID)
			{
				if(!DoesHaveLabel(cond->additionalSortedListLabel))
					labels_to_add.push_back(cond->additionalSortedListLabel);
			}

			break;
		}

		case ENT_QUERY_WEIGHTED_SAMPLE:
		case ENT_QUERY_AMONG:
		case ENT_QUERY_NOT_AMONG:
		case ENT_QUERY_MIN:
		case ENT_QUERY_MAX:
		case ENT_QUERY_MIN_DIFFERENCE:
		case ENT_QUERY_MAX_DIFFERENCE:
		{
			if(!DoesHaveLabel(cond->singleLabel))
				labels_to_add.push_back(cond->singleLabel);

			break;
		}

		case ENT_QUERY_SUM:
		case ENT_QUERY_MODE:
		case ENT_QUERY_QUANTILE:
		case ENT_QUERY_GENERALIZED_MEAN:
		case ENT_QUERY_VALUE_MASSES:
		{
			if(!DoesHaveLabel(cond->singleLabel))
				labels_to_add.push_back(cond->singleLabel);

			if(cond->weightLabel != StringInternPool::NOT_A_STRING_ID)
			{
				if(!DoesHaveLabel(cond->weightLabel))
					labels_to_add.push_back(cond->weightLabel);
			}

			break;
		}

		case ENT_QUERY_EXISTS:
		case ENT_QUERY_NOT_EXISTS:
		{
			for(auto label : cond->existLabels)
			{
				if(!DoesHaveLabel(label))
					labels_to_add.push_back(label);
			}
			break;
		}

		case ENT_QUERY_EQUALS:
		case ENT_QUERY_NOT_EQUALS:
		{
			for(auto &[label_id, _] : cond->singleLabels)
			{
				if(!DoesHaveLabel(label_id))
					labels_to_add.push_back(label_id);
			}
			break;
		}

		default:
		{
			for(auto &[label_id, _] : cond->pairedLabels)
			{
				if(!DoesHaveLabel(label_id))
					labels_to_add.push_back(label_id);
			}
		}
	}


	if(labels_to_add.size() == 0)
		return;

#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	lock.unlock();
	Concurrency::WriteLock write_lock(mutex);

	//now with write_lock, remove any labels that might have already been added by other threads
	labels_to_add.erase(std::remove_if(begin(labels_to_add), end(labels_to_add),
		[this](auto sid) { return DoesHaveLabel(sid); }),
		end(labels_to_add));

	//need to double-check to make sure that another thread didn't already rebuild
	if(labels_to_add.size() > 0)
#endif
		sbfds.AddLabels(labels_to_add, container->GetContainedEntities());

#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	//release write lock and reacquire read lock
	write_lock.unlock();
	lock.lock();
#endif
}

void EntityQueryCaches::GetMatchingEntities(EntityQueryCondition *cond, BitArrayIntegerSet &matching_entities,
	std::vector<DistanceReferencePair<size_t>> &compute_results, bool is_first, bool update_matching_entities)
{
#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	Concurrency::ReadLock lock(mutex);
	EnsureLabelsAreCached(cond, lock);
#else
	EnsureLabelsAreCached(cond);
#endif

	switch(cond->queryType)
	{
		case ENT_QUERY_EXISTS:
		{
			for(auto label : cond->existLabels)
			{
				if(is_first)
				{
					sbfds.FindAllEntitiesWithFeature(label, matching_entities);
					is_first = false;
				}
				else
					sbfds.IntersectEntitiesWithFeature(label, matching_entities);
			}
			return;
		}

		case ENT_QUERY_NOT_EXISTS:
		{
			for(auto label : cond->existLabels)
			{
				if(is_first)
				{
					sbfds.FindAllEntitiesWithoutFeature(label, matching_entities);
					is_first = false;
				}
				else
					sbfds.IntersectEntitiesWithoutFeature(label, matching_entities);
			}
			return;
		}

		case ENT_QUERY_NEAREST_GENERALIZED_DISTANCE:
		case ENT_QUERY_WITHIN_GENERALIZED_DISTANCE:
		case ENT_COMPUTE_ENTITY_CONVICTIONS:
		case ENT_COMPUTE_ENTITY_KL_DIVERGENCES:
		case ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE:
		case ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS:
		{
			//get entity (case) weighting if applicable
			bool use_entity_weights = (cond->weightLabel != StringInternPool::NOT_A_STRING_ID);
			size_t weight_column = std::numeric_limits<size_t>::max();
			if(use_entity_weights)
				weight_column = sbfds.GetColumnIndexFromLabelId(cond->weightLabel);

			auto get_weight = sbfds.GetNumberValueFromEntityIndexFunction(weight_column);
			EntityQueriesStatistics::DistanceTransform<size_t> distance_transform(cond->transformSuprisalToProb,
				cond->distanceWeightExponent, use_entity_weights, get_weight);

			if(cond->queryType == ENT_QUERY_NEAREST_GENERALIZED_DISTANCE || cond->queryType == ENT_QUERY_WITHIN_GENERALIZED_DISTANCE)
			{
				//labels and values must have the same size
				if(cond->valueToCompare.size() != cond->positionLabels.size())
				{
					matching_entities.clear();
					return;
				}

				//if first, need to populate with all entities
				if(is_first)
				{
					matching_entities.clear();
					matching_entities.SetAllIds(sbfds.GetNumInsertedEntities());
				}

				//if no position labels, then the weight must be zero so just randomly choose k
				if(cond->positionLabels.size() == 0)
				{
					BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;
					temp = matching_entities;
					matching_entities.clear();

					auto rand_stream = cond->randomStream.CreateOtherStreamViaRand();

					//insert each case and compute to zero distance because the distance because weight was zero to get here
					size_t num_to_retrieve = std::min(static_cast<size_t>(cond->maxToRetrieve), temp.size());
					for(size_t i = 0; i < num_to_retrieve; i++)
					{
						size_t rand_index = temp.GetRandomElement(rand_stream);
						temp.erase(rand_index);
						matching_entities.insert(rand_index);
						compute_results.emplace_back(0.0, rand_index);
					}
				}
				else if(cond->queryType == ENT_QUERY_NEAREST_GENERALIZED_DISTANCE)
				{
					sbfds.FindNearestEntities(cond->distParams, cond->positionLabels, cond->valueToCompare, cond->valueTypes,
						static_cast<size_t>(cond->maxToRetrieve), cond->exclusionLabel, matching_entities,
						compute_results, cond->randomStream.CreateOtherStreamViaRand());
				}
				else //ENT_QUERY_WITHIN_GENERALIZED_DISTANCE
				{
					sbfds.FindEntitiesWithinDistance(cond->distParams, cond->positionLabels, cond->valueToCompare, cond->valueTypes,
						cond->maxDistance, matching_entities, compute_results);
				}

				distance_transform.TransformDistances(compute_results, cond->returnSortedList);

				//populate matching_entities if needed
				if(update_matching_entities)
				{
					matching_entities.clear();
					for(auto &it : compute_results)
						matching_entities.insert(it.reference);
				}
			}
			else //cond->queryType ==  ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS or ENT_COMPUTE_ENTITY_CONVICTIONS or ENT_COMPUTE_ENTITY_KL_DIVERGENCES or ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE
			{
				size_t total_contained_entities = sbfds.GetNumInsertedEntities();
				if(total_contained_entities == 0)
					return;

				//if there are no existLabels, or number of existLabels is same as the number of entities in cache, we don't compute on subset
				const bool compute_on_subset = (cond->existLabels.size() != 0 && cond->existLabels.size() < total_contained_entities);

				size_t top_k = std::min(static_cast<size_t>(cond->maxToRetrieve), total_contained_entities);
					
				BitArrayIntegerSet *ents_to_compute_ptr = nullptr; //if nullptr, compute is done on all entities in the cache

				if(compute_on_subset) //if subset is specified, set ents_to_compute_ptr to set of ents_to_compute
				{
					ents_to_compute_ptr = &buffers.tempMatchingEntityIndices;
					ents_to_compute_ptr->clear();

					if(cond->queryType == ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE)
					{
						//determine the base entities by everything not in the list
						*ents_to_compute_ptr = matching_entities;

						for(auto entity_sid : cond->existLabels)
						{
							size_t entity_index = container->GetContainedEntityIndex(entity_sid);
							ents_to_compute_ptr->erase(entity_index);
						}
					}
					else
					{
						for(auto entity_sid : cond->existLabels)
						{
							size_t entity_index = container->GetContainedEntityIndex(entity_sid);
							if(entity_index != std::numeric_limits<size_t>::max())
								ents_to_compute_ptr->insert(entity_index);
						}

						//make sure everything asked to be computed is in the base set of entities
						ents_to_compute_ptr->Intersect(matching_entities);
					}
				}
				else //compute on all
				{
					ents_to_compute_ptr = &matching_entities;
				}

				//only select cases that have all of the correct features
				for(auto i : cond->positionLabels)
					sbfds.IntersectEntitiesWithFeature(i, *ents_to_compute_ptr);

			#ifdef MULTITHREAD_SUPPORT
				ConvictionProcessor<KnnNonZeroDistanceQuerySBFCache, size_t, BitArrayIntegerSet> conviction_processor(buffers.convictionBuffers,
					buffers.knnCache, distance_transform, top_k, cond->useConcurrency);
			#else
				ConvictionProcessor<KnnNonZeroDistanceQuerySBFCache, size_t, BitArrayIntegerSet> conviction_processor(buffers.convictionBuffers,
					buffers.knnCache, distance_transform, top_k);
			#endif
				buffers.knnCache.ResetCache(sbfds, matching_entities, cond->distParams, cond->positionLabels);

				auto &results_buffer = buffers.doubleVector;
				results_buffer.clear();

				if(cond->queryType == ENT_COMPUTE_ENTITY_CONVICTIONS)
				{
					conviction_processor.ComputeCaseKLDivergences(*ents_to_compute_ptr, results_buffer, true, cond->convictionOfRemoval);
				}
				else if(cond->queryType == ENT_COMPUTE_ENTITY_KL_DIVERGENCES)
				{
					conviction_processor.ComputeCaseKLDivergences(*ents_to_compute_ptr, results_buffer, false, cond->convictionOfRemoval);
				}
				else if(cond->queryType == ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE)
				{
					double group_conviction = conviction_processor.ComputeCaseGroupKLDivergence(*ents_to_compute_ptr, cond->convictionOfRemoval);

					compute_results.clear();
					compute_results.emplace_back(group_conviction, 0);

					//early exit because don't need to translate distances
					return;
				}
				else //ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS
				{
					conviction_processor.ComputeDistanceContributions(ents_to_compute_ptr, results_buffer);
				}

				//clear compute_results as it may have been used for intermediate results
				compute_results.clear();
				if(ents_to_compute_ptr == nullptr)
				{
					//computed on globals, so convert results to global coordinates paired with their contributions
					compute_results.reserve(results_buffer.size());

					for(size_t i = 0; i < results_buffer.size(); i++)
						compute_results.emplace_back(results_buffer[i], i);
				}
				else //computed on a subset; use ents_to_compute_ptr because don't know what it points to
				{
					compute_results.reserve(ents_to_compute_ptr->size());
					size_t i = 0;
					for(const auto &ent_index : *ents_to_compute_ptr)
						compute_results.emplace_back(results_buffer[i++], ent_index);
				}

				if(cond->returnSortedList)
				{
					std::sort(begin(compute_results), end(compute_results),
						[](auto a, auto b) {return a.distance < b.distance; }
					);
				}
			}

			break;
		}

		case ENT_QUERY_EQUALS:
			{
				bool first_feature = is_first;

				//loop over all features
				for(size_t i = 0; i < cond->singleLabels.size(); i++)
				{
					auto &[label_id, compare_value] = cond->singleLabels[i];
					auto compare_type = cond->valueTypes[i];

					if(first_feature)
					{
						matching_entities.clear();
						sbfds.UnionAllEntitiesWithValue(label_id, compare_type, compare_value, matching_entities);
						first_feature = false;
					}
					else //get corresponding indices and intersect with results
					{
						BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;
						temp.clear();
						sbfds.UnionAllEntitiesWithValue(label_id, compare_type, compare_value, temp);
						matching_entities.Intersect(temp);
					}
				}

				break;
			}

		case ENT_QUERY_NOT_EQUALS:
		{
			bool first_feature = is_first;

			//loop over all features
			for(size_t i = 0; i < cond->singleLabels.size(); i++)
			{
				auto &[label_id, compare_value] = cond->singleLabels[i];
				auto compare_type = cond->valueTypes[i];

				if(first_feature)
				{
					matching_entities.clear();
					sbfds.FindAllEntitiesWithFeature(label_id, matching_entities);
					first_feature = false;
				}

				BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;
				temp.clear();
				sbfds.UnionAllEntitiesWithValue(label_id, compare_type, compare_value, temp);
				matching_entities.EraseInBatch(temp);
			}
			matching_entities.UpdateNumElements();

			break;
		}

		case ENT_QUERY_BETWEEN:
		case ENT_QUERY_NOT_BETWEEN:
			{
				bool first_feature = is_first;
				BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;

				//loop over all features
				for(size_t i = 0; i < cond->pairedLabels.size(); i++)
				{
					auto label_id = cond->pairedLabels[i].first;
					auto &[low_value, high_value] = cond->pairedLabels[i].second;

					if(first_feature)
					{
						sbfds.FindAllEntitiesWithinRange(label_id, cond->valueTypes[i],
							low_value, high_value, matching_entities, cond->queryType == ENT_QUERY_BETWEEN);
						first_feature = false;
					}
					else //get corresponding indices and intersect with results
					{
						temp.clear();
						sbfds.FindAllEntitiesWithinRange(label_id, cond->valueTypes[i],
							low_value, high_value, temp, cond->queryType == ENT_QUERY_BETWEEN);
						matching_entities.Intersect(temp);
					}
				}

				break;
			}

		case ENT_QUERY_MIN:
		case ENT_QUERY_MAX:
		{
			size_t max_to_retrieve = static_cast<size_t>(cond->maxToRetrieve);

			if(is_first)
			{
				sbfds.FindMinMax(cond->singleLabel, cond->singleLabelType, max_to_retrieve,
									(cond->queryType == ENT_QUERY_MAX), nullptr, matching_entities);
			}
			else
			{
				//move data to temp and compute into matching_entities
				BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;
				temp = matching_entities;
				matching_entities.clear();
				sbfds.FindMinMax(cond->singleLabel, cond->singleLabelType, max_to_retrieve,
									(cond->queryType == ENT_QUERY_MAX), &temp, matching_entities);
			}
			break;
		}

		case ENT_QUERY_AMONG:
		{
			if(is_first)
			{
				for(size_t i = 0; i < cond->valueToCompare.size(); i++)
					sbfds.UnionAllEntitiesWithValue(cond->singleLabel, cond->valueTypes[i], cond->valueToCompare[i], matching_entities);
			}
			else
			{
				//get set of entities that are valid
				BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;
				temp.clear();
				for(size_t i = 0; i < cond->valueToCompare.size(); i++)
					sbfds.UnionAllEntitiesWithValue(cond->singleLabel, cond->valueTypes[i], cond->valueToCompare[i], temp);

				//only keep those that have a matching value
				matching_entities.Intersect(temp);
			}

			break;
		}

		case ENT_QUERY_NOT_AMONG:
		{
			//ensure that the feature exists
			if(is_first)
				sbfds.FindAllEntitiesWithFeature(cond->singleLabel, matching_entities);
			else
				sbfds.IntersectEntitiesWithFeature(cond->singleLabel, matching_entities);

			BitArrayIntegerSet &temp = buffers.tempMatchingEntityIndices;
			temp.clear();
			//get set of entities that are valid
			for(size_t i = 0; i < cond->valueToCompare.size(); i++)
				sbfds.UnionAllEntitiesWithValue(cond->singleLabel, cond->valueTypes[i], cond->valueToCompare[i], temp);

			//only keep those that have a matching value
			matching_entities.erase(temp);

			break;
		}

		case ENT_QUERY_SUM:
		case ENT_QUERY_MODE:
		case ENT_QUERY_QUANTILE:
		case ENT_QUERY_GENERALIZED_MEAN:
		case ENT_QUERY_MIN_DIFFERENCE:
		case ENT_QUERY_MAX_DIFFERENCE:
		{
			size_t column_index = sbfds.GetColumnIndexFromLabelId(cond->singleLabel);
			if(column_index == std::numeric_limits<size_t>::max())
			{
				compute_results.emplace_back(std::numeric_limits<double>::quiet_NaN(), 0);
				return;
			}

			size_t weight_column_index = sbfds.GetColumnIndexFromLabelId(cond->weightLabel);
			bool has_weight = false;
			if(weight_column_index != std::numeric_limits<size_t>::max())
				has_weight = true;
			else //just use a valid column
				weight_column_index = 0;

			double result = 0.0;

			if(is_first)
			{
				EfficientIntegerSet &entities = sbfds.GetEntitiesWithValidNumbers(column_index);
				auto get_value = sbfds.GetNumberValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(column_index);
				auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(weight_column_index);

				switch(cond->queryType)
				{
				case ENT_QUERY_SUM:
					result = EntityQueriesStatistics::Sum(entities.begin(), entities.end(), get_value, has_weight, get_weight);
					break;

				case ENT_QUERY_MODE:
					result = EntityQueriesStatistics::ModeNumber(entities.begin(), entities.end(), get_value, has_weight, get_weight);
					break;

				case ENT_QUERY_QUANTILE:
					result = EntityQueriesStatistics::Quantile(entities.begin(), entities.end(), get_value,
						has_weight, get_weight, cond->qPercentage, EntityQueryCaches::buffers.pairDoubleVector);
					break;

				case ENT_QUERY_GENERALIZED_MEAN:
					result = EntityQueriesStatistics::GeneralizedMean(entities.begin(), entities.end(), get_value,
						has_weight, get_weight, cond->distParams.pValue, cond->center, cond->calculateMoment, cond->absoluteValue);
					break;

				case ENT_QUERY_MIN_DIFFERENCE:
					result = EntityQueriesStatistics::ExtremeDifference(entities.begin(), entities.end(), get_value, true,
						cond->maxDistance, cond->includeZeroDifferences, EntityQueryCaches::buffers.doubleVector);
					break;

				case ENT_QUERY_MAX_DIFFERENCE:
					result = EntityQueriesStatistics::ExtremeDifference(entities.begin(), entities.end(), get_value, false,
						cond->maxDistance, cond->includeZeroDifferences, EntityQueryCaches::buffers.doubleVector);
					break;

				default:
					break;
				}
			}
			else
			{
				auto get_value = sbfds.GetNumberValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(column_index);
				auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(weight_column_index);

				switch(cond->queryType)
				{
				case ENT_QUERY_SUM:
					result = EntityQueriesStatistics::Sum(matching_entities.begin(), matching_entities.end(), get_value, has_weight, get_weight);
					break;

				case ENT_QUERY_MODE:
					result = EntityQueriesStatistics::ModeNumber(matching_entities.begin(), matching_entities.end(), get_value, has_weight, get_weight);
					break;

				case ENT_QUERY_QUANTILE:
					result = EntityQueriesStatistics::Quantile(matching_entities.begin(), matching_entities.end(), get_value,
						has_weight, get_weight, cond->qPercentage, EntityQueryCaches::buffers.pairDoubleVector);
					break;

				case ENT_QUERY_GENERALIZED_MEAN:
					result = EntityQueriesStatistics::GeneralizedMean(matching_entities.begin(), matching_entities.end(), get_value,
						has_weight, get_weight, cond->distParams.pValue, cond->center, cond->calculateMoment, cond->absoluteValue);
					break;

				case ENT_QUERY_MIN_DIFFERENCE:
					result = EntityQueriesStatistics::ExtremeDifference(matching_entities.begin(), matching_entities.end(), get_value, true,
						cond->maxDistance, cond->includeZeroDifferences, EntityQueryCaches::buffers.doubleVector);
					break;

				case ENT_QUERY_MAX_DIFFERENCE:
					result = EntityQueriesStatistics::ExtremeDifference(matching_entities.begin(), matching_entities.end(), get_value, false,
						cond->maxDistance, cond->includeZeroDifferences, EntityQueryCaches::buffers.doubleVector);
					break;

				default:
					break;
				}
			}

			compute_results.emplace_back(result, 0);
			return;
		}

		default:  // Other Enum value not handled
		{
			break;
		}
	}
}

bool EntityQueryCaches::ComputeValueFromMatchingEntities(EntityQueryCondition *cond, BitArrayIntegerSet &matching_entities,
	StringInternPool::StringID &compute_result, bool is_first)
{
#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	Concurrency::ReadLock lock(mutex);
	EnsureLabelsAreCached(cond, lock);
#else
	EnsureLabelsAreCached(cond);
#endif

	switch(cond->queryType)
	{
	case ENT_QUERY_MODE:
	{
		size_t column_index = sbfds.GetColumnIndexFromLabelId(cond->singleLabel);
		if(column_index == std::numeric_limits<size_t>::max())
			return false;

		size_t weight_column_index = sbfds.GetColumnIndexFromLabelId(cond->weightLabel);
		bool has_weight = false;
		if(weight_column_index != std::numeric_limits<size_t>::max())
			has_weight = true;
		else //just use a valid column
			weight_column_index = 0;

		if(is_first)
		{
			EfficientIntegerSet &entities = sbfds.GetEntitiesWithValidStringIds(column_index);
			auto get_value = sbfds.GetStringIdValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(column_index);
			auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(weight_column_index);
			auto [found, mode_id] = EntityQueriesStatistics::ModeStringId(
				entities.begin(), entities.end(), get_value, has_weight, get_weight);

			compute_result = mode_id;
			return found;
		}
		else
		{
			auto get_value = sbfds.GetStringIdValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(column_index);
			auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(weight_column_index);
			auto [found, mode_id] = EntityQueriesStatistics::ModeStringId(
				matching_entities.begin(), matching_entities.end(), get_value, has_weight, get_weight);

			compute_result = mode_id;
			return found;
		}
	}
	default:
		break;
	}

	return false;
}

void EntityQueryCaches::ComputeValuesFromMatchingEntities(EntityQueryCondition *cond, BitArrayIntegerSet &matching_entities,
	FastHashMap<double, double, std::hash<double>, DoubleNanHashComparator> &compute_results, bool is_first)
{
#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	Concurrency::ReadLock lock(mutex);
	EnsureLabelsAreCached(cond, lock);
#else
	EnsureLabelsAreCached(cond);
#endif
	
	switch(cond->queryType)
	{
		case ENT_QUERY_VALUE_MASSES:
		{
			size_t column_index = sbfds.GetColumnIndexFromLabelId(cond->singleLabel);
			if(column_index == std::numeric_limits<size_t>::max())
				return;

			size_t weight_column_index = sbfds.GetColumnIndexFromLabelId(cond->weightLabel);
			bool has_weight = false;
			if(weight_column_index != std::numeric_limits<size_t>::max())
				has_weight = true;
			else //just use a valid column
				weight_column_index = 0;

			size_t num_unique_values = sbfds.GetNumUniqueValuesForColumn(column_index, ENIVT_NUMBER);

			if(is_first)
			{
				EfficientIntegerSet &entities = sbfds.GetEntitiesWithValidNumbers(column_index);
				auto get_value = sbfds.GetNumberValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(column_index);
				auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(weight_column_index);
				compute_results = EntityQueriesStatistics::ValueMassesNumber(entities.begin(), entities.end(),
					num_unique_values, get_value, has_weight, get_weight);
			}
			else
			{
				auto get_value = sbfds.GetNumberValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(column_index);
				auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(weight_column_index);
				compute_results = EntityQueriesStatistics::ValueMassesNumber(matching_entities.begin(), matching_entities.end(),
					num_unique_values, get_value, has_weight, get_weight);			
			}
			return;
		}
		default:
			break;
	}
}

void EntityQueryCaches::ComputeValuesFromMatchingEntities(EntityQueryCondition *cond, BitArrayIntegerSet &matching_entities,
	FastHashMap<StringInternPool::StringID, double> &compute_results, bool is_first)
{
#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	Concurrency::ReadLock lock(mutex);
	EnsureLabelsAreCached(cond, lock);
#else
	EnsureLabelsAreCached(cond);
#endif

	switch(cond->queryType)
	{
	case ENT_QUERY_VALUE_MASSES:
	{
		size_t column_index = sbfds.GetColumnIndexFromLabelId(cond->singleLabel);
		if(column_index == std::numeric_limits<size_t>::max())
			return;
	
		size_t weight_column_index = sbfds.GetColumnIndexFromLabelId(cond->weightLabel);
		bool has_weight = false;
		if(weight_column_index != std::numeric_limits<size_t>::max())
			has_weight = true;
		else //just use a valid column
			weight_column_index = 0;
	
		size_t num_unique_values = sbfds.GetNumUniqueValuesForColumn(column_index, ENIVT_STRING_ID);
	
		if(is_first)
		{
			EfficientIntegerSet &entities = sbfds.GetEntitiesWithValidStringIds(column_index);
			auto get_value = sbfds.GetStringIdValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(column_index);
			auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<EfficientIntegerSet::Iterator>(weight_column_index);
			compute_results = EntityQueriesStatistics::ValueMassesStringId(entities.begin(), entities.end(),
				num_unique_values, get_value, has_weight, get_weight);
		}
		else
		{
			auto get_value = sbfds.GetStringIdValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(column_index);
			auto get_weight = sbfds.GetNumberValueFromEntityIteratorFunction<BitArrayIntegerSet::Iterator>(weight_column_index);
			compute_results = EntityQueriesStatistics::ValueMassesStringId(matching_entities.begin(), matching_entities.end(),
				num_unique_values, get_value, has_weight, get_weight);
		}
	
		return;
	}
	default:
		break;
	}
}

void EntityQueryCaches::GetMatchingEntitiesViaSamplingWithReplacement(EntityQueryCondition *cond, BitArrayIntegerSet &matching_entities, std::vector<size_t> &entity_indices_sampled, bool is_first, bool update_matching_entities)
{
#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	Concurrency::ReadLock lock(mutex);
	EnsureLabelsAreCached(cond, lock);
#else
	EnsureLabelsAreCached(cond);
#endif

	size_t num_to_sample = static_cast<size_t>(cond->maxToRetrieve);

	auto &probabilities = EntityQueryCaches::buffers.doubleVector;
	auto &entity_indices = EntityQueryCaches::buffers.entityIndices;

	if(is_first)
		sbfds.FindAllEntitiesWithValidNumbers(cond->singleLabel, matching_entities, entity_indices, probabilities);
	else
		sbfds.IntersectEntitiesWithValidNumbers(cond->singleLabel, matching_entities, entity_indices, probabilities);

	//don't attempt to continue if no elements
	if(matching_entities.size() == 0)
		return;		

	if(update_matching_entities)
		matching_entities.clear();

	NormalizeProbabilities(probabilities);

	//if not sampling many, then brute force it
	if(num_to_sample < 10)
	{
		//sample the entities
		for(size_t i = 0; i < num_to_sample; i++)
		{
			size_t selected_entity_index = WeightedDiscreteRandomSample(probabilities, cond->randomStream);
			auto eid = entity_indices[selected_entity_index];

			if(update_matching_entities)
				matching_entities.insert(eid);
			else
				entity_indices_sampled.push_back(eid);
		}
	}
	else //sampling a bunch, better to precompute and use faster method
	{
		//a table for quickly generating entity indices based on weights
		WeightedDiscreteRandomStreamTransform<StringInternPool::StringID, CompactHashMap<size_t, double>> ewt(entity_indices, probabilities, false);

		//sample the entities
		for(size_t i = 0; i < num_to_sample; i++)
		{
			auto eid = ewt.WeightedDiscreteRand(cond->randomStream);

			if(update_matching_entities)
				matching_entities.insert(eid);
			else
				entity_indices_sampled.push_back(eid);
		}
	}
}

bool EntityQueryCaches::DoesCachedConditionMatch(EntityQueryCondition *cond, bool last_condition)
{
	EvaluableNodeType qt = cond->queryType;

	if(qt == ENT_QUERY_NEAREST_GENERALIZED_DISTANCE || qt == ENT_QUERY_WITHIN_GENERALIZED_DISTANCE || qt == ENT_COMPUTE_ENTITY_CONVICTIONS
			|| qt == ENT_COMPUTE_ENTITY_GROUP_KL_DIVERGENCE || qt == ENT_COMPUTE_ENTITY_DISTANCE_CONTRIBUTIONS || qt ==  ENT_COMPUTE_ENTITY_KL_DIVERGENCES)
	{
		//does not allow radii
		if(cond->singleLabel != StringInternPool::NOT_A_STRING_ID)
			return false;

		//TODO 4948: sbfds does not fully support p0 acceleration; it requires templating and calling logs of differences, then performing an inverse transform at the end
		if(cond->distParams.pValue == 0)
			return false;

		return true;
	}

	return true;
}
