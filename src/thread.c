#include <c4/thread.h>
#include <c4/mm/slab.h>
#include <c4/mm/region.h>
#include <c4/common.h>
#include <c4/debug.h>

static slab_t thread_slab;
static unsigned thread_counter = 0;

void init_threading( void ){
	static bool initialized = false;

	if ( !initialized ){
		slab_init_at( &thread_slab, region_get_global(),
	  	              sizeof(thread_t), NULL, NULL );

		initialized = true;
	}
}

thread_t *thread_create( void (*entry)(void *),
                         void *data,
                         page_dir_t *dir,
                         void *stack,
                         unsigned flags )
{
	thread_t *ret = slab_alloc( &thread_slab );

	thread_set_init_state( ret, entry, data, stack, flags );

	ret->id       = thread_counter++;
	ret->task_id  = 1;
	ret->page_dir = dir;
	ret->flags    = flags;

	return ret;
}

thread_t *thread_create_kthread( void (*entry)(void *), void *data ){
	uint8_t *stack = region_alloc( region_get_global( ));

	KASSERT( stack != NULL );
	stack += PAGE_SIZE;

	return thread_create( entry,
	                      data,
	                      page_get_kernel_dir(),
	                      stack,
	                      THREAD_FLAG_NONE );
}

void thread_destroy( thread_t *thread ){
	slab_free( &thread_slab, thread );
}

// TODO: maybe merge the slab list functions with the functions here
//       using a common doubly-linked list implementation
void thread_list_insert( thread_list_t *list, thread_t *thread ){
	thread->list = list;
	thread->next = list->first;
	thread->prev = NULL;

	if ( list->first ){
		list->first->prev = thread;
	}

	list->first = thread;
}

void thread_list_remove( thread_t *thread ){
	if ( thread->list ){
		if ( thread->prev ){
			thread->prev->next = thread->next;
		}

		if ( thread->next ){
			thread->next->prev = thread->prev;
		}

		if ( thread == thread->list->first ){
			thread->list->first = thread->next;
		}
	}
}

thread_t *thread_list_pop( thread_list_t *list ){
	thread_t *ret = NULL;

	if ( list->first ){
		ret = list->first;
		thread_list_remove( list->first );
	}

	return ret;
}

thread_t *thread_list_peek( thread_list_t *list ){
	thread_t *ret = NULL;

	if ( list->first ){
		ret = list->first;
	}

	return ret;
}
