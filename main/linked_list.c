

#include "linked_list.h"

void ll_init(linked_list_t *list) {
    list->head = NULL;
}

void ll_init_item(linked_list_item_t *item) {
    item->next = NULL;
}


void ll_append(linked_list_t *list, void *_item) {
    linked_list_item_t *item = (linked_list_item_t *) _item;
    item->next = NULL;

    linked_list_item_t *ptr = list->head;
    if (ptr) {
        while (ptr->next) {
            ptr = ptr->next;
        }
        ptr->next = item;
    } else {
        // There is no head, so the list is empty.
        list->head = item;
    }
}

void ll_remove(linked_list_t *list, void *_item) {
    linked_list_item_t *item = (linked_list_item_t *) _item;

    if (list->head == item) {
        // Its the head that we're removing
        list->head = list->head->next;
        item->next = NULL;
    } else {
        linked_list_item_t *prev = list->head;
        while (prev && prev->next != item) {
            prev = prev->next;
        }
        if (prev) {
            prev->next = item->next;
            item->next = NULL;
        } else {
            // Item not found!  Silently ignore it?
        }
    }
}