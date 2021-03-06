/**
 * (C) Copyright 2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * Record timestamp table
 * vos/vos_ts.h
 *
 * Author: Jeff Olivier <jeffrey.v.olivier@intel.com>
 */

#ifndef __VOS_TS__
#define __VOS_TS__

#include <vos_tls.h>

struct vos_ts_info {
	/** Least recently accessed index */
	uint32_t		ti_lru;
	/** Most recently accessed index */
	uint32_t		ti_mru;
	/** Type identifier */
	uint32_t		ti_type;
	/** mask for hash of negative entries */
	uint32_t		ti_cache_mask;
	/** Number of entries in cache for type (for testing) */
	uint32_t		ti_count;
};

struct vos_ts_entry {
	struct vos_ts_info	*te_info;
	/** Uniquely identifies the record */
	void			*te_record_ptr;
	/** Uniquely identifies the parent record */
	uint32_t		*te_parent_ptr;
	/** negative entry cache */
	uint32_t		*te_miss_idx;
	/** Low read time or read time for the object/key */
	daos_epoch_t		 te_ts_rl;
	/** Max read time for subtrees */
	daos_epoch_t		 te_ts_rh;
	/** Write time */
	daos_epoch_t		 te_ts_w;
	/** uuid's of transactions.  These can potentially be changed
	 *  to 16 bits and save some space here.  But for now, stick
	 *  with the full id.
	 */
	/** Low read tx */
	uuid_t			 te_tx_rl;
	/** high read tx */
	uuid_t			 te_tx_rh;
	/** write tx */
	uuid_t			 te_tx_w;
	/** Next most recently used */
	uint32_t		 te_next_idx;
	/** Previous most recently used */
	uint32_t		 te_prev_idx;
	/** Hash index in parent */
	uint32_t		 te_hash_idx;
};

struct vos_ts_set_entry {
	/** Pointer to the entry at this level */
	struct vos_ts_entry	*se_entry;
	/** pointer to newly created index */
	uint32_t		*se_create_idx;
	/** Cache of calculated hash for obj/key */
	uint64_t		 se_hash;
};

/** Structure looking up and caching operation flags */
struct vos_ts_set {
	/** Operation flags */
	uint64_t		 ts_flags;
	/** size of the set */
	uint32_t		 ts_set_size;
	/** Number of initialized entries */
	uint32_t		 ts_init_count;
	/** timestamp entries */
	struct vos_ts_set_entry	 ts_entries[0];
};

/** Table will be per xstream */
#define VOS_TS_BITS	23
#define VOS_TS_SIZE	(1 << VOS_TS_BITS)
#define VOS_TS_MASK	(VOS_TS_SIZE - 1)

/** Timestamp types */
#define D_FOREACH_TS_TYPE(ACTION)					\
	ACTION(VOS_TS_TYPE_CONT, "container", 1024,       32 * 1024)	\
	ACTION(VOS_TS_TYPE_OBJ,  "object",    96 * 1024,  128 * 1024)	\
	ACTION(VOS_TS_TYPE_DKEY, "dkey",      896 * 1024, 1024 * 1024)	\
	ACTION(VOS_TS_TYPE_AKEY, "akey",      0,          0)		\

#define DEFINE_TS_TYPE(type, desc, count, child_count)	type, type##_CHILD,

enum {
	D_FOREACH_TS_TYPE(DEFINE_TS_TYPE)
	/** Number of timestamp types */
	VOS_TS_TYPE_COUNT = VOS_TS_TYPE_AKEY_CHILD,
};

struct vos_ts_table {
	/** Global read low timestamp for type */
	daos_epoch_t		tt_ts_rl;
	/** Global read high timestamp for type */
	daos_epoch_t		tt_ts_rh;
	/** Global write timestamp for type */
	daos_epoch_t		tt_ts_w;
	/** Timestamp table pointers for a type */
	struct vos_ts_info	tt_type_info[VOS_TS_TYPE_COUNT];
	/** The table entries */
	struct vos_ts_entry	tt_table[VOS_TS_SIZE];
};

/** Internal API: Evict the LRU, move it to MRU, update relevant time stamps,
 *  and return the index
 */
void
vos_ts_evict_lru(struct vos_ts_table *ts_table, struct vos_ts_entry *parent,
		 struct vos_ts_entry **entryp, uint32_t *idx, uint32_t hash_idx,
		 uint32_t type);

/** Internal API: Evict selected entry from the cache, update global
 *  timestamps
 */
void
vos_ts_evict_entry(struct vos_ts_table *ts_table, struct vos_ts_entry *entry,
		   uint32_t idx);

/** Internal API: Remove an entry from the lru list */
static inline void
remove_ts_entry(struct vos_ts_entry *entries, struct vos_ts_entry *entry)
{
	struct vos_ts_entry	*prev = &entries[entry->te_prev_idx];
	struct vos_ts_entry	*next = &entries[entry->te_next_idx];

	prev->te_next_idx = entry->te_next_idx;
	next->te_prev_idx = entry->te_prev_idx;
}

/** Internal API: Insert an entry in the lru list */
static inline void
insert_ts_entry(struct vos_ts_entry *entries, struct vos_ts_entry *entry,
		uint32_t idx, uint32_t prev_idx, uint32_t next_idx)
{
	struct vos_ts_entry	*prev;
	struct vos_ts_entry	*next;

	prev = &entries[prev_idx];
	next = &entries[next_idx];
	next->te_prev_idx = idx;
	prev->te_next_idx = idx;
	entry->te_prev_idx = prev_idx;
	entry->te_next_idx = next_idx;
}

/** Internal API: Make the entry the mru */
static inline void
move_lru(struct vos_ts_table *ts_table, struct vos_ts_entry *entry,
	 uint32_t idx)
{
	struct vos_ts_info	*info = entry->te_info;

	if (info->ti_mru == idx) {
		/** Already the mru */
		return;
	}

	if (info->ti_lru == idx)
		info->ti_lru = entry->te_next_idx;

	/** First remove */
	remove_ts_entry(&ts_table->tt_table[0], entry);

	/** Now add */
	insert_ts_entry(&ts_table->tt_table[0], entry, idx, info->ti_mru,
			info->ti_lru);

	info->ti_mru = idx;
}

/** Internal API: Grab the parent entry from the set */
static inline struct vos_ts_entry *
ts_set_get_parent(struct vos_ts_set *ts_set)
{
	struct vos_ts_set_entry	*set_entry;
	struct vos_ts_entry	*parent = NULL;
	uint32_t		 parent_set_idx;

	D_ASSERT(ts_set->ts_set_size != ts_set->ts_init_count);
	if (ts_set->ts_init_count > 0) {
		/** 2 is dkey index in case there are multiple akeys */
		parent_set_idx = MIN(ts_set->ts_init_count - 1, 2);
		set_entry = &ts_set->ts_entries[parent_set_idx];
		parent = set_entry->se_entry;
	}

	return parent;

}

/** Internal API to lookup entry from index */
static inline struct vos_ts_entry *
vos_ts_lookup_idx(struct vos_ts_table *ts_table, uint32_t *idx)
{
	struct vos_ts_entry	*entry;
	uint32_t		 tindex = *idx & VOS_TS_MASK;

	entry = &ts_table->tt_table[tindex];
	if (entry->te_record_ptr == idx) {
		move_lru(ts_table, entry, tindex);
		return entry;
	}

	return NULL;
}

/** Reset the index in the set so an entry can be replaced
 *
 * \param	ts_set[in]	The timestamp set
 * \param	type[in]	Type of entry
 * \param	akey_idx[in]	Set to 0 if not akey, otherwise idx of akey
 */
static inline void
vos_ts_set_reset(struct vos_ts_set *ts_set, uint32_t type, uint32_t akey_nr)
{
	uint32_t	idx;

	if (ts_set == NULL)
		return;

	D_ASSERT((type == VOS_TS_TYPE_AKEY) || (akey_nr == 0));
	D_ASSERT((type & 1) == 0);
	idx = type / 2 + akey_nr;
	D_ASSERT(idx <= ts_set->ts_init_count);
	ts_set->ts_init_count = idx;
}

/** Lookup an entry in the timestamp cache and save it to the set.
 *
 * \param	ts_set[in]	The timestamp set
 * \param	idx[in,out]	Address of the entry index.
 * \param	reset[in]	Remove the last entry in the set before checking
 * \param	entryp[in,out]	Valid only if function returns true.  Will be
 *				NULL if ts_set is NULL.
 *
 * \return true if the timestamp set is NULL or the entry is found in cache
 */
static inline bool
vos_ts_lookup(struct vos_ts_set *ts_set, uint32_t *idx, bool reset,
	      struct vos_ts_entry **entryp)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get();
	struct vos_ts_entry	*entry;
	struct vos_ts_set_entry	 set_entry = {0};

	*entryp = NULL;

	if (ts_set == NULL)
		return true;

	if (reset)
		ts_set->ts_init_count--;

	ts_table = vos_ts_table_get();

	entry = vos_ts_lookup_idx(ts_table, idx);
	if (entry != NULL) {
		D_ASSERT(ts_set->ts_set_size != ts_set->ts_init_count);
		set_entry.se_entry = entry;
		ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;
		*entryp = entry;
		return true;
	}

	return false;
}

/** Allocate a new entry in the set.   Lookup should be called first and this
 * should only be called if it returns false.
 *
 * \param	ts_set[in]	The timestamp set
 * \param	idx[in,out]	Address of the entry index.
 * \param	hash[in]	Hash to identify the item
 *
 * \return	Returns a pointer to the entry or NULL if ts_set is not
 *		allocated.
 */
static inline struct vos_ts_entry *
vos_ts_alloc(struct vos_ts_set *ts_set, uint32_t *idx, uint64_t hash)
{
	struct vos_ts_entry	*parent;
	struct vos_ts_table	*ts_table;
	struct vos_ts_info	*info;
	struct vos_ts_set_entry	 set_entry = {0};
	struct vos_ts_entry	*new_entry;
	uint32_t		 hash_idx;
	uint32_t		 new_type = 0;

	if (ts_set == NULL)
		return NULL;

	ts_table = vos_ts_table_get();

	parent = ts_set_get_parent(ts_set);

	if (parent == NULL) {
		hash_idx = 0;
		info = &ts_table->tt_type_info[0];
	} else {
		info = parent->te_info;
		/* Allocated entry must have a real parent */
		D_ASSERT((info->ti_type & 1) == 0);
		hash_idx = hash & info->ti_cache_mask;
		new_type = info->ti_type + 2;
	}

	D_ASSERT((info->ti_type & 1) == 0);
	D_ASSERT(info->ti_type != VOS_TS_TYPE_AKEY);

	vos_ts_evict_lru(ts_table, parent, &new_entry, idx, hash_idx,
			 new_type);

	set_entry.se_entry = new_entry;
	/** No need to save allocation hash for non-negative entry */
	ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;
	return new_entry;
}

/** Get the last entry in the set
 *
 * \param	ts_set[in]	The timestamp set
 *
 * \return Returns the last entry added to the set or NULL
 */
static inline struct vos_ts_entry *
vos_ts_set_get_entry(struct vos_ts_set *ts_set)
{
	struct vos_ts_set_entry	*entry;

	if (ts_set == NULL || ts_set->ts_init_count == 0)
		return NULL;

	entry = &ts_set->ts_entries[ts_set->ts_init_count - 1];
	return entry->se_entry;
}

/** Get the specified entry in the set
 *
 * \param	ts_set[in]	The timestamp set
 * \param	type[in]	The type of entry
 * \param	akey_idx[in]	0 or index of the akey
 *
 * \return Returns the last entry added to the set or NULL
 */
static inline struct vos_ts_entry *
vos_ts_set_get_entry_type(struct vos_ts_set *ts_set, uint32_t type,
			  int akey_idx)
{
	struct vos_ts_set_entry	*entry;
	uint32_t		 idx = (type / 2) + akey_idx;

	D_ASSERT(akey_idx == 0 || type == VOS_TS_TYPE_AKEY);

	if (ts_set == NULL || idx >= ts_set->ts_init_count)
		return NULL;

	entry = &ts_set->ts_entries[idx];
	return entry->se_entry;
}

/** Set the index of the associated positive entry in the last entry
 *  in the set.
 *
 *  \param	ts_set[in]	The timestamp set
 *  \param	idx[in]		Pointer to the index that will be used
 *				when allocating the positive entry
 */
static inline void
vos_ts_set_mark_entry(struct vos_ts_set *ts_set, uint32_t *idx)
{
	struct vos_ts_set_entry	*entry;

	if (ts_set == NULL || ts_set->ts_init_count == 0)
		return;

	entry = &ts_set->ts_entries[ts_set->ts_init_count - 1];

	/** Should be a negative entry */
	D_ASSERT(entry->se_entry->te_info->ti_type & 1);
	entry->se_create_idx = idx;
}

/** When a subtree doesn't exist, we need a negative entry.  The entry in this
 *  case is identified by a hash.  This looks up the negative entry and
 *  allocates it if necessary.  Resets te_create_idx to NULL.
 *
 * \param	ts_set[in]	The timestamp set
 * \param	hash		The hash of the missing subtree entry
 * \param	reset[in]	Remove the last entry in the set before checking
 *
 * \return	The entry for negative lookups on the subtree
 */
static inline struct vos_ts_entry *
vos_ts_get_negative(struct vos_ts_set *ts_set, uint64_t hash, bool reset)
{
	struct vos_ts_entry	*parent;
	struct vos_ts_info	*info;
	struct vos_ts_entry	*neg_entry;
	struct vos_ts_table	*ts_table;
	struct vos_ts_set_entry	 set_entry = {0};
	uint32_t		 idx;

	if (ts_set == NULL)
		return NULL;

	if (reset)
		ts_set->ts_init_count--;

	parent = ts_set_get_parent(ts_set);

	D_ASSERT(parent != NULL);

	ts_table = vos_ts_table_get();

	info = parent->te_info;
	if (info->ti_type & 1) {
		/** Parent is a negative entry, just reuse it
		 *  for child entry
		 */
		neg_entry = parent;
		goto add_to_set;
	}

	idx = hash & info->ti_cache_mask;
	if (vos_ts_lookup(ts_set, &parent->te_miss_idx[idx], false, &neg_entry))
		goto out;

	vos_ts_evict_lru(ts_table, parent, &neg_entry,
			 &parent->te_miss_idx[idx], idx, info->ti_type + 1);
add_to_set:
	set_entry.se_entry = neg_entry;
	ts_set->ts_entries[ts_set->ts_init_count++] = set_entry;
out:
	ts_set->ts_entries[ts_set->ts_init_count-1].se_hash = hash;

	return neg_entry;
}

/** If an entry is still in the thread local timestamp cache, evict it and
 *  update global timestamps for the type.  Move the evicted entry to the LRU
 *  and mark it as already evicted.
 *
 * \param	idx[in]		Address of the entry index.
 * \param	type[in]	Type of the object
 */
static inline void
vos_ts_evict(uint32_t *idx)
{
	struct vos_ts_table	*ts_table = vos_ts_table_get();
	struct vos_ts_entry	*entry;
	uint32_t		 tindex = *idx & VOS_TS_MASK;

	entry = &ts_table->tt_table[tindex];
	if (entry->te_record_ptr != idx)
		return;

	vos_ts_evict_entry(ts_table, entry, *idx);
}

/** Allocate thread local timestamp cache.   Set the initial global times
 *
 * \param	ts_table[in,out]	Thread local table pointer
 *
 * \return	-DER_NOMEM	Not enough memory available
 *		0		Success
 */
int
vos_ts_table_alloc(struct vos_ts_table **ts_table);


/** Free the thread local timestamp cache and reset pointer to NULL
 *
 * \param	ts_table[in,out]	Thread local table pointer
 */
void
vos_ts_table_free(struct vos_ts_table **ts_table);

/** Update the low read timestamp, if applicable
 *
 *  \param	entry[in,out]	Entry to update
 *  \param	epoch[in]	Update epoch
 */
static inline void
vos_ts_update_read_low(struct vos_ts_entry *entry, daos_epoch_t epoch)
{
	if (entry == NULL)
		return;
	entry->te_ts_rl = MAX(entry->te_ts_rl, epoch);
}

/** Update the high read timestamp, if applicable
 *
 *  \param	entry[in,out]	Entry to update
 *  \param	epoch[in]	Update epoch
 */
static inline void
vos_ts_update_read_high(struct vos_ts_entry *entry, daos_epoch_t epoch)
{
	if (entry == NULL)
		return;
	entry->te_ts_rh = MAX(entry->te_ts_rh, epoch);
}

/** Update the write timestamp, if applicable
 *
 *  \param	entry[in,out]	Entry to update
 *  \param	epoch[in]	Update epoch
 */
static inline void
vos_ts_update_write(struct vos_ts_entry *entry, daos_epoch_t epoch,
		    const uuid_t xid)
{
	if (entry == NULL)
		return;

	if (entry->te_ts_w >= epoch)
		return;

	entry->te_ts_w = epoch;
	uuid_copy(entry->te_tx_w, xid);
}

/** Allocate a timestamp set
 *
 * \param	ts_set[in,out]	Pointer to set
 * \param	flags[in]	Operations flags
 * \param	akey_nr[in]	Number of akeys in operation
 *
 * \return	0 on success, error otherwise.
 */
int
vos_ts_set_allocate(struct vos_ts_set **ts_set, uint64_t flags,
		    uint32_t akey_nr);

/** Upgrade any negative entries in the set now that the associated
 *  update/punch has committed
 *
 *  \param	ts_set[in]	Pointer to set
 */
void
vos_ts_set_upgrade(struct vos_ts_set *ts_set);

/** Free an allocated timestamp set
 *
 * \param	ts_set[in]	Set to free
 */
static inline void
vos_ts_set_free(struct vos_ts_set *ts_set)
{
	D_FREE(ts_set);
}

/** Check the read low timestamp at current entry.
 *
 * \param	ts_set[in]	The timestamp set
 * \param	write_time[in]	The write time
 *
 * \return	true	Conflict
 *		false	No conflict (or no timestamp set)
 */
static inline bool
vos_ts_check_rl_conflict(struct vos_ts_set *ts_set, daos_epoch_t write_time)
{
	struct vos_ts_entry	*entry;

	entry = vos_ts_set_get_entry(ts_set);
	if (entry == NULL || write_time > entry->te_ts_rl)
		return false;

	/** TODO: Need to eventually handle == case but it should not be an
	 * issue without MVCC.
	 */
	return true;
}

/** Check the read high timestamp at current entry.
 *
 * \param	ts_set[in]	The timestamp set
 * \param	write_time[in]	The write time
 *
 * \return	true	Conflict
 *		false	No conflict (or no timestamp set)
 */
static inline bool
vos_ts_check_rh_conflict(struct vos_ts_set *ts_set, daos_epoch_t write_time)
{
	struct vos_ts_entry	*entry;

	entry = vos_ts_set_get_entry(ts_set);
	if (entry == NULL || write_time > entry->te_ts_rh)
		return false;

	/** TODO: Need to eventually handle == case but it should not be an
	 * issue without MVCC.
	 */
	return true;
}

#endif /* __VOS_TS__ */
