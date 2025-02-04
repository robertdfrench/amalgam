#pragma once

//project headers:
#include "Entity.h"
#include "EvaluableNodeTreeManipulation.h"
#include "Merger.h"

//Contains various classes and functions to manipulate entities
class EntityManipulation
{
public:
	//functionality to merge two Entities
	class EntitiesMergeMethod : public Merger<Entity *>
	{
	public:
		constexpr EntitiesMergeMethod(Interpreter *_interpreter, bool keep_all_of_both)
			: interpreter(_interpreter), keepAllOfBoth(keep_all_of_both)
		{	}

		virtual MergeMetricResults<Entity *> MergeMetric(Entity *a, Entity *b)
		{
			return NumberOfSharedNodes(a, b);
		}

		virtual Entity *MergeValues(Entity *a, Entity *b, bool must_merge = false);

		virtual bool KeepAllNonMergeableValues()
		{	return keepAllOfBoth;	}

		virtual bool KeepSomeNonMergeableValues()
		{	return keepAllOfBoth;	}

		virtual bool KeepNonMergeableValue()
		{	return keepAllOfBoth;	}

		virtual bool KeepNonMergeableAInsteadOfB()
		{	return keepAllOfBoth;	}

		virtual bool KeepNonMergeableA()
		{	return keepAllOfBoth;	}
		virtual bool KeepNonMergeableB()
		{	return keepAllOfBoth;	}

		virtual bool AreMergeable(Entity *a, Entity *b)
		{	return keepAllOfBoth;	}

		Interpreter *interpreter;

	protected:
		bool keepAllOfBoth;
	};

	//functionality to difference two Entities
	// merged entities will *not* contain any code, this is simply for mapping which entities should be merged
	class EntitiesMergeForDifferenceMethod : public EntitiesMergeMethod
	{
	public:
		inline EntitiesMergeForDifferenceMethod(Interpreter *_interpreter)
			: EntitiesMergeMethod(_interpreter, false)
		{	}

		virtual Entity *MergeValues(Entity *a, Entity *b, bool must_merge = false);

		constexpr CompactHashMap<Entity *, Entity *> &GetAEntitiesIncludedFromB()
		{	return aEntitiesIncludedFromB;		}
		constexpr CompactHashMap<Entity *, std::pair<Entity *, bool>> &GetMergedEntitiesIncludedFromB()
		{	return mergedEntitiesIncludedFromB;		}

	protected:
		//key is the entity contained (perhaps deeply) by b
		CompactHashMap<Entity *, Entity *> aEntitiesIncludedFromB;
		//key is the entity contained (perhaps deeply) by b
		// value is a pair, the first being the entity from the merged entity group and the second being a bool as to whether or not the code is identical
		CompactHashMap<Entity *, std::pair<Entity *, bool>> mergedEntitiesIncludedFromB;
	};

	//functionality to mix Entities
	class EntitiesMixMethod : public EntitiesMergeMethod
	{
	public:
		EntitiesMixMethod(Interpreter *_interpreter,
			double fraction_a, double fraction_b, double similar_mix_chance, double fraction_entities_to_mix);

		virtual Entity *MergeValues(Entity *a, Entity *b, bool must_merge);

		virtual bool KeepAllNonMergeableValues()
		{	return false;	}

		virtual bool KeepSomeNonMergeableValues()
		{	return true;	}

		virtual bool KeepNonMergeableValue()
		{
			return interpreter->randomStream.Rand() < fractionAOrB;
		}

		virtual bool KeepNonMergeableAInsteadOfB()
		{
			return interpreter->randomStream.Rand() < fractionAInsteadOfB;
		}

		virtual bool KeepNonMergeableA()
		{
			return interpreter->randomStream.Rand() < fractionA;
		}
		virtual bool KeepNonMergeableB()
		{
			return interpreter->randomStream.Rand() < fractionB;
		}

		virtual bool AreMergeable(Entity *a, Entity *b)
		{
			return interpreter->randomStream.Rand() < fractionEntitiesToMix;
		}

	protected:

		double fractionA;
		double fractionB;
		double fractionAOrB;
		double fractionAInsteadOfB;
		double similarMixChance;
		double fractionEntitiesToMix;
	};

	//Entity merging functions
	static Entity *IntersectEntities(Interpreter *interpreter, Entity *entity1, Entity *entity2);

	static Entity *UnionEntities(Interpreter *interpreter, Entity *entity1, Entity *entity2);

	//returns code that will transform entity1 into entity2, allocated with enm
	static EvaluableNodeReference DifferenceEntities(Interpreter *interpreter, Entity *entity1, Entity *entity2);

	static Entity *MixEntities(Interpreter *interpreter, Entity *entity1, Entity *entity2,
		double fractionA, double fractionB, double similar_mix_chance, double fraction_entities_to_mix);

	//Computes the total number of nodes in both trees that are equal
	static MergeMetricResults<Entity *> NumberOfSharedNodes(Entity *entity1, Entity *entity2);

	//computes the edit distance between the two entities
	static double EditDistance(Entity *entity1, Entity *entity2);

	static Entity *MutateEntity(Interpreter *interpreter, Entity *entity, double mutation_rate, CompactHashMap<StringInternPool::StringID, double> *mutation_weights, CompactHashMap<EvaluableNodeType, double> *operation_type);

	//flattens entity using interpreter into code that can recreate it
	// if include_rand_seeds is true, it will emit code that includes them; otherwise it won't
	// if parallel_create is true, it will emit slightly more complex code that creates entities in parallel
	static EvaluableNodeReference FlattenEntity(Interpreter *interpreter, Entity *entity, bool include_rand_seeds, bool parallel_create);

protected:

	//creates an associative lookup of the entities contained by entity from the string id to the entity pointer
	static inline Entity::EntityLookupAssocType CreateContainedEntityLookupByStringId(Entity *entity)
	{
		Entity::EntityLookupAssocType contained_entities_lookup;
		if(entity != nullptr)
		{
			auto &contained_entities = entity->GetContainedEntities();
			contained_entities_lookup.reserve(contained_entities.size());
			for(auto ce : entity->GetContainedEntities())
				contained_entities_lookup.insert(std::make_pair(ce->GetIdStringId(), ce));
		}
		return contained_entities_lookup;
	}

	//adds to merged_entity's contained entities to consist of Entities that are common across all of the Entities specified
	//merged_entity should already have its code merged, as MergeContainedEntities may edit the strings in merged_entity to update
	// new names of merged contained entities
	static void MergeContainedEntities(EntitiesMergeMethod *mm, Entity *entity1, Entity *entity2, Entity *merged_entity);

	//traverses entity and all contained entities and for each of the entities, finds any string that matches a key of
	// entities_renamed and replaces it with the value
	//assumes that entity is not nullptr
	static void RecursivelyRenameAllEntityReferences(Entity *entity, CompactHashMap<StringInternPool::StringID, StringInternPool::StringID> &entities_renamed);
};
