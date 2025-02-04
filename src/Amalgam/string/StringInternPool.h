#pragma once

//project headers:
#include "Concurrency.h"
#include "HashMaps.h"
#include "PlatformSpecific.h"

//system headers:
#include <queue>
#include <string>
#include <vector>

//manages all strings so they can be referred and compared easily by integers, across threads
//depends on a method defined outside of this class, StringInternPool::InitializeStaticStrings()
// to set up all internal static strings; see the function's declaration for details
class StringInternPool
{
public:
	using StringID = size_t;
	using StringToStringIDAssoc = FastHashMap<std::string, StringID>;

	//indicates that it is not a string, like NaN or null
	static constexpr size_t NOT_A_STRING_ID = 0;
	static constexpr size_t EMPTY_STRING_ID = 1;
	inline static const std::string EMPTY_STRING = std::string("");

	inline StringInternPool()
	{
		InitializeStaticStrings();
	}

	//translates the id to a string, empty string if it does not exist
	const std::string &GetStringFromID(StringID id);

	//translates the string to the corresponding ID, 0 is the empty string, maximum value of size_t means it does not exist
	StringID GetIDFromString(const std::string &str);

	//makes a new reference to the string specified, returning the ID
	StringID CreateStringReference(const std::string &str);

	//makes a new reference to the string id specified, returning the id passed in
	StringID CreateStringReference(StringID id);

	//creates new references from the references container and function
	template<typename ReferencesContainer,
		typename GetStringIdFunction = StringInternPool::StringID(StringInternPool::StringID)>
	inline void CreateStringReferences(ReferencesContainer &references_container,
		GetStringIdFunction get_string_id = [](auto sid) { return sid;  })
	{
		if(references_container.size() == 0)
			return;

	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		//only need a ReadLock because the count is atomic
		Concurrency::ReadLock lock(sharedMutex);
	#endif

		for(auto r : references_container)
		{
			StringID id = get_string_id(r);
			if(IsStringIDStatic(id))
				continue;

			IncrementRefCount(id);
		}
	}

	//creates additional_reference_count new references from the references container and function
	// specialized for size_t indexed containers, where the index is desired
	template<typename ReferencesContainer,
		typename GetStringIdFunction = StringInternPool::StringID(StringInternPool::StringID)>
	inline void CreateMultipleStringReferences(ReferencesContainer &references_container,
		size_t additional_reference_count,
		GetStringIdFunction get_string_id = [](auto sid) { return sid;  })
	{
		if(references_container.size() == 0)
			return;

	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		//only need a ReadLock because the count is atomic
		Concurrency::ReadLock lock(sharedMutex);
	#endif

		for(auto r : references_container)
		{
			StringID id = get_string_id(r);
			if(IsStringIDStatic(id))
				continue;

			AdvanceRefCount(id, additional_reference_count);
		}
	}

	//creates new references from the references container and function
	// specialized for size_t indexed containers, where the index is desired
	template<typename ReferencesContainer,
		typename GetStringIdFunction = StringInternPool::StringID(StringInternPool::StringID)>
	inline void CreateStringReferencesByIndex(ReferencesContainer &references_container,
		GetStringIdFunction get_string_id = [](auto sid) { return sid;  })
	{
		if(references_container.size() == 0)
			return;

	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		//only need a ReadLock because the count is atomic
		Concurrency::ReadLock lock(sharedMutex);
	#endif

		for(size_t i = 0; i < references_container.size(); i++)
		{
			StringID id = get_string_id(references_container[i], i);
			if(IsStringIDStatic(id))
				continue;

			IncrementRefCount(id);
		}
	}

	//removes a reference to the string specified by the ID
	void DestroyStringReference(StringID id);

	//creates new references from the references container and function
	template<typename ReferencesContainer,
		typename GetStringIdFunction = StringInternPool::StringID(StringInternPool::StringID)>
	inline void DestroyStringReferences(ReferencesContainer &references_container,
		GetStringIdFunction get_string_id = [](auto sid) { return sid;  })
	{
	#if !defined(MULTITHREAD_SUPPORT) && !defined(MULTITHREAD_INTERFACE)
		for(auto r : references_container)
			DestroyStringReference(get_string_id(r));
	#else
		if(references_container.size() == 0)
			return;

		//only need a ReadLock because the count is atomic
		Concurrency::ReadLock lock(sharedMutex);

		//as it goes through, if any id needs removal, will set this to true so that
		// removal can be done after refernce count decreases are done
		bool ids_need_removal = false;

		for(auto r : references_container)
		{
			StringID id = get_string_id(r);
			if(IsStringIDStatic(id))
				continue;

			int64_t refcount = DecrementRefCount(id);

			//if extra references, just return, but if it is 1, then it will try to clear
			if(refcount == 1)
				ids_need_removal = true;
		}

		if(!ids_need_removal)
			return;

		//need to remove at least one reference, so put all counts back while wait for write lock
		for(auto r : references_container)
		{
			StringID id = get_string_id(r);
			if(!IsStringIDStatic(id))
				IncrementRefCount(id);
		}

		//grab a write lock
		lock.unlock();
		Concurrency::WriteLock write_lock(sharedMutex);

		for(auto r : references_container)
		{
			StringID id = get_string_id(r);
			if(IsStringIDStatic(id))
				continue;

			//remove any that are the last reference
			int64_t refcount = DecrementRefCount(id);
			if(refcount == 1)
				RemoveId(id);
		}

	#endif
	}

	//returns the number of strings that are still allocated
	//even when "empty" it will still return 2 since the NOT_A_STRING_ID and EMPTY_STRING_ID take up slots
	inline size_t GetNumStringsInUse()
	{	return stringToID.size();	}

	//returns the number of non-static strings that are still in use
	size_t GetNumDynamicStringsInUse()
	{
	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		Concurrency::ReadLock lock(sharedMutex);
	#endif

		size_t count = 0;
		for(const auto &it : stringToID)
		{
			if(!IsStringIDStatic(it.second))
				count++;
		}
		return count;
	}

	//returns the number of non-static string references that are currently in use
	int64_t GetNumNonStaticStringReferencesInUse()
	{
	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		Concurrency::ReadLock lock(sharedMutex);
	#endif

		int64_t count = 0;
		for(size_t id = 0; id < idToStringAndRefCount.size(); id++)
		{
			if(!IsStringIDStatic(id))
				count += idToStringAndRefCount[id].second;
		}
		return count;
	}

	//returns a vector of all the strings still in use.  Intended for debugging.
	std::vector<std::string> GetNonStaticStringsInUse()
	{
	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		Concurrency::ReadLock lock(sharedMutex);
	#endif

		std::vector<std::string> in_use;
		for(size_t id = 0; id < idToStringAndRefCount.size(); id++)
		{
			if(!IsStringIDStatic(id) && idToStringAndRefCount[id].second > 0)
				in_use.push_back(idToStringAndRefCount[id].first);
		}
		return in_use;
	}

	//returns true if the string associated with stringID id is a static string
	constexpr bool IsStringIDStatic(StringID id)
	{
		return id < numStaticStrings; //static strings must begin at id 0, so numStaticStrings represents the first string id that is not static
	}

protected:

	//increments the reference count and returns the previous reference count
	inline int64_t IncrementRefCount(StringID id)
	{
	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		//perform an atomic increment so that it can be done under a read lock
		//TODO 15993: once C++20 is widely supported, change type to atomic_ref
		return reinterpret_cast<std::atomic<int64_t>&>(idToStringAndRefCount[id].second).fetch_add(1);
	#else
		return idToStringAndRefCount[id].second++;
	#endif
	}

	//adds advancement to the reference count
	inline void AdvanceRefCount(StringID id, size_t advancement)
	{
	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		//perform an atomic increment so that it can be done under a read lock
		//TODO 15993: once C++20 is widely supported, change type to atomic_ref
		reinterpret_cast<std::atomic<int64_t>&>(idToStringAndRefCount[id].second).fetch_add(advancement);
	#else
		idToStringAndRefCount[id].second += advancement;
	#endif
	}

	//decrements the reference count and returns the previous reference count
	inline int64_t DecrementRefCount(StringID id)
	{
	#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
		//perform an atomic decrement so that it can be done under a read lock
		//TODO 15993: once C++20 is widely supported, change type to atomic_ref
		return reinterpret_cast<std::atomic<int64_t>&>(idToStringAndRefCount[id].second).fetch_sub(1);
	#else
		return idToStringAndRefCount[id].second--;
	#endif
	}

	//removes everything associated with the id
	inline void RemoveId(StringID id)
	{
		//removed last reference; clear the string and free memory
		stringToID.erase(idToStringAndRefCount[id].first);
		idToStringAndRefCount[id].first = "";
		idToStringAndRefCount[id].first.shrink_to_fit();
		unusedIDs.push(id);
	}

	//must be defined outside of this class and initialize all static strings
	//needs to set numStaticStrings and call EmplaceStaticString for each StringID from 0 up to numStaticStrings
	// with the respective string
	//the first two strings MUST be not-a-string followed by empty string
	void InitializeStaticStrings();

	//sets string id sid to str, assuming the position has already been allocated in idToStringAndRefCount
	inline void EmplaceStaticString(StringID sid, const char *str)
	{
		idToStringAndRefCount[sid] = std::make_pair(str, 0);
		stringToID.emplace(str, sid);
	}

	//mapping from ID (index) to the string and the number of references
	//use a signed counter in case it goes negative such that comparisons work well even if multiple threads have freed it
	std::vector<std::pair<std::string, int64_t>> idToStringAndRefCount;

	//mapping from string to ID (index of idToStringAndRefCount)
	StringToStringIDAssoc stringToID;

	//IDs (indexes of idToStringAndRefCount) that are now unused
	std::priority_queue<StringID, std::vector<StringID>, std::greater<StringID> > unusedIDs;

	//number of static strings
	size_t numStaticStrings;

#if defined(MULTITHREAD_SUPPORT) || defined(MULTITHREAD_INTERFACE)
	Concurrency::ReadWriteMutex sharedMutex;
#endif
};

extern StringInternPool string_intern_pool;

//A reference to a string
//maintains reference counts and will clear upon destruction
class StringInternRef
{
public:
	constexpr StringInternRef() : id(StringInternPool::NOT_A_STRING_ID)
	{	}

	inline StringInternRef(StringInternPool::StringID sid)
	{
		id = string_intern_pool.CreateStringReference(sid);
	}

	inline StringInternRef(const std::string &str)
	{
		id = string_intern_pool.CreateStringReference(str);
	}

	//copy constructor
	inline StringInternRef(const StringInternRef &sir)
	{
		id = string_intern_pool.CreateStringReference(sir.id);
	}

	inline ~StringInternRef()
	{
		string_intern_pool.DestroyStringReference(id);
	}

	//easy-to-read way of creating an empty string
	inline static StringInternRef EmptyString()
	{	return StringInternRef();	}

	//assign another string reference
	inline StringInternRef &operator =(const StringInternRef &sir)
	{
		if(id != sir.id)
		{
			string_intern_pool.DestroyStringReference(id);
			id = string_intern_pool.CreateStringReference(sir.id);
		}
		return *this;
	}

	//allow being able to use as a string
	inline operator const std::string &()
	{
		return string_intern_pool.GetStringFromID(id);
	}

	//allow being able to use as a string id
	constexpr operator StringInternPool::StringID()
	{
		return id;
	}

	//call this to set the id and create a reference
	inline void SetIDAndCreateReference(StringInternPool::StringID sid)
	{
		//if changing id, need to delete previous
		if(id > string_intern_pool.EMPTY_STRING_ID && id != sid)
			string_intern_pool.DestroyStringReference(id);

		if(id != sid)
		{
			id = sid;
			string_intern_pool.CreateStringReference(id);
		}
	}

	//only call this when the sid already has a reference and this is being used to manage it
	inline void SetIDWithReferenceHandoff(StringInternPool::StringID sid)
	{
		if(id > string_intern_pool.EMPTY_STRING_ID)
		{
			//if the ids are different, then need to delete old
			//if the ids are the same, then have a duplicate reference, so need to delete one
			//so delete a reference either way
			string_intern_pool.DestroyStringReference(id);
		}

		id = sid;
	}

private:

	StringInternPool::StringID id;
};

//A weak reference to a string
// When the string does not exist, it will take on the value of the empty string
class StringInternWeakRef
{
public:
	constexpr StringInternWeakRef()
		: id(StringInternPool::NOT_A_STRING_ID)
	{	}

	constexpr StringInternWeakRef(StringInternPool::StringID sid)
		: id(sid)
	{	}

	StringInternWeakRef(const std::string &str)
	{
		id = string_intern_pool.GetIDFromString(str);
	}

	constexpr StringInternWeakRef(const StringInternWeakRef &siwr)
		: id(siwr.id)
	{	}

	//easy-to-read way of creating an empty string
	inline static StringInternRef EmptyString()
	{
		return StringInternRef();
	}

	//allow being able to use as a string
	inline operator const std::string &()
	{
		return string_intern_pool.GetStringFromID(id);
	}

	//allow being able to use as a string id
	constexpr operator StringInternPool::StringID()
	{
		return id;
	}

	//only call this when the sid already has a reference and this is being used to manage it
	constexpr void SetID(StringInternPool::StringID sid)
	{
		id = sid;
	}

private:

	StringInternPool::StringID id;
};
