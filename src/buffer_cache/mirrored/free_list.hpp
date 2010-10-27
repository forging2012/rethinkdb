#ifndef __BUFFER_CACHE_MIRRORED_FREE_LIST_HPP__
#define __BUFFER_CACHE_MIRRORED_FREE_LIST_HPP__

#include "serializer/serializer.hpp"
#include "buffer_cache/types.hpp"
#include "containers/segmented_vector.hpp"

/* TODO combine array_free_list_t with array_map_t because they happen to conveniently
never overlap */

template<class mc_config_t>
class array_free_list_t {
    
public:
    array_free_list_t(mc_cache_t<mc_config_t> *);
    
    struct ready_callback_t {
        virtual void on_free_list_ready() = 0;
    };
    bool start(ready_callback_t *cb);   // Must be called after serializer has started up
    
    ~array_free_list_t();
    
    int num_blocks_in_use;
    block_id_t gen_block_id();
    void release_block_id(block_id_t);

private:
    ready_callback_t *ready_callback;
    int max_block_id;
    bool do_get_size();   // Called on serializer CPU
    bool have_gotten_size(ser_block_id_t max_block_id);   // Called on cache CPU
    bool do_make_list();   // Called on serializer CPU
    bool have_made_list();   // Called on cache CPU
    
    mc_cache_t<mc_config_t> *cache;
    segmented_vector_t<block_id_t, MAX_BLOCK_ID> free_list;
    block_id_t first_block;
    
    perfmon_var_t<int> pm_num_blocks_in_use;
};

#include "free_list.tcc"

#endif /* __BUFFER_CACHE_MIRRORED_FREE_LIST_HPP__ */
