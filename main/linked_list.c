

#include "linked_list.h"

void ll_init(linked_list_t *list) {
    list->head = NULL;
    list->tail = NULL;
}

void ll_init_item(linked_list_item_t *item) {
    item->next = NULL;
    item->prev = NULL;
}


void ll_append(linked_list_t *list, void *_item) {
    linked_list_item_t *item = (linked_list_item_t *) _item;
    item->next = NULL;
    if (list->head) {
        list->tail->next = item;
        item->prev = list->tail;
    } else {
        list->head = list->tail = item;
        item->prev = NULL;
    }
}

void ll_remove(linked_list_t *list, void *_item) {
    linked_list_item_t *item = (linked_list_item_t *) _item;

    if (item->prev) {
        item->prev->next = item->next;
    } else {
        // It was the first
        list->head = item->next;
    }
    if (item->next) {
        item->next->prev = item->prev;
    } else {
        list->tail = item->prev;
    }
    item->next = item->prev = NULL;
}