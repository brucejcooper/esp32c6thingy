

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <cbor.h>

typedef struct linked_list_item_t {
    struct linked_list_item_t *next;
    struct linked_list_item_t *prev;
} linked_list_item_t;


typedef struct linked_list_t {
    linked_list_item_t *head;
    linked_list_item_t *tail;
} linked_list_t;




void ll_init(linked_list_t *list);
void ll_init_item(linked_list_item_t *item);
void ll_append(linked_list_t *list, void *_item);
void ll_remove(linked_list_t *list, void *_item);



#ifdef __cplusplus
}
#endif
