/*
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 *
 * Sensitive Data Protection
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
 
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>

#include <linux/slab.h>

#include <linux/spinlock.h>
#include <linux/list.h>

#include <sdp/dek_common.h>

typedef struct __kek_pack {
	int userid;

	struct list_head list;

	struct list_head kek_list_head;
	spinlock_t kek_list_lock;
}kek_pack_t;

typedef struct __kek_item {
	struct list_head list;

	int kek_type;
	kek_t kek;
}kek_item_t;

#define KEK_PACK_DEBUG		0

#if KEK_PACK_DEBUG
#define KEK_PACK_LOGD(FMT, ...) printk("KEK_PACK[%d] %s :: " FMT , current->pid, __func__, ##__VA_ARGS__)
#else
#define KEK_PACK_LOGD(FMT, ...)
#endif /* PUB_CRYPTO_DEBUG */
#define KEK_PACK_LOGE(FMT, ...) printk("KEK_PACK[%d] %s :: " FMT , current->pid, __func__, ##__VA_ARGS__)

struct list_head kek_pack_list_head;
spinlock_t kek_pack_list_lock;

void init_kek_pack(void) {
	spin_lock_init(&kek_pack_list_lock);
	INIT_LIST_HEAD(&kek_pack_list_head);
}

static kek_pack_t *find_kek_pack(int userid) {
	struct list_head *entry;

	spin_lock(&kek_pack_list_lock);

	list_for_each(entry, &kek_pack_list_head) {
		kek_pack_t *pack = list_entry(entry, kek_pack_t, list);

		if(pack->userid == userid) {
			KEK_PACK_LOGE("Found kek-pack : %d\n", userid);
			spin_unlock(&kek_pack_list_lock);
			return pack;
		}
	}
	spin_unlock(&kek_pack_list_lock);

	KEK_PACK_LOGE("Can't find kek-pack : %d\n", userid);

	return NULL;
}

static int __add_kek(kek_pack_t *pack, kek_t *kek) {
	kek_item_t *item;

	if(kek == NULL) return -EINVAL;
	if(pack == NULL) return -EINVAL;

	item = kmalloc(sizeof(kek_item_t), GFP_KERNEL);
	if(item == NULL) return -ENOMEM;

	INIT_LIST_HEAD(&item->list);
	item->kek_type = kek->type;
	memcpy(&item->kek, kek, sizeof(kek_t));

	spin_lock(&pack->kek_list_lock);
	list_add_tail(&item->list, &pack->kek_list_head);
	spin_unlock(&pack->kek_list_lock);

    KEK_PACK_LOGD("item %p\n", item);

	return 0;
}

static kek_item_t *find_kek_item(kek_pack_t *pack, int kek_type) {
	struct list_head *entry;

	if(pack == NULL) return NULL;

	spin_lock(&pack->kek_list_lock);
	list_for_each(entry, &pack->kek_list_head) {
		kek_item_t *item = list_entry(entry, kek_item_t, list);

		if(item->kek_type == kek_type) {
			KEK_PACK_LOGE("Found kek-item : %d\n", kek_type);
			spin_unlock(&pack->kek_list_lock);

			return item;
		}
	}
	spin_unlock(&pack->kek_list_lock);

	KEK_PACK_LOGE("Can't find kek %d : %d\n", kek_type, pack->userid);

	return NULL;
}

static void del_kek_item(kek_item_t *item) {
    KEK_PACK_LOGD("entered\n");

    if(item) {
        list_del(&item->list);
        kzfree(item);
    } else {
        KEK_PACK_LOGD("given item is NULL\n");
    }
}

int add_kek_pack(int userid) {
	kek_pack_t *new_kek_pack;

	KEK_PACK_LOGD("entered\n");

	if(find_kek_pack(userid)) {
		KEK_PACK_LOGE("kek-pack for %d already exists\n", userid);
		return -EEXIST;
	}

	new_kek_pack = kmalloc(sizeof(kek_pack_t), GFP_KERNEL);
	if(new_kek_pack == NULL) {
		KEK_PACK_LOGE("can't alloc memory for kek_pack_t\n");
		return -EINVAL;
	}

	new_kek_pack->userid = userid;
	spin_lock_init(&new_kek_pack->kek_list_lock);
	INIT_LIST_HEAD(&new_kek_pack->kek_list_head);

	spin_lock(&kek_pack_list_lock);
	list_add_tail(&new_kek_pack->list, &kek_pack_list_head);
	spin_unlock(&kek_pack_list_lock);

	return 0;
}

void del_kek_pack(int userid) {
	struct list_head *entry, *q;
	kek_pack_t *pack;

	KEK_PACK_LOGD("entered\n");
	pack = find_kek_pack(userid);
	if(pack == NULL) return;

	spin_lock(&pack->kek_list_lock);
	list_for_each_safe(entry, q, &pack->kek_list_head) {
		kek_item_t *item = list_entry(entry, kek_item_t, list);
		KEK_PACK_LOGD("entry %p item %p\n", entry, item);
		del_kek_item(item);
	}
	spin_unlock(&pack->kek_list_lock);

	list_del(&pack->list);
	kzfree(pack);
}

int add_kek(int userid, kek_t *kek) {
	int rc;
	kek_pack_t *pack;

	KEK_PACK_LOGD("entered\n");
	pack = find_kek_pack(userid);
	if(pack == NULL) return -ENOENT;

	if(find_kek_item(pack, kek->type)) return -EEXIST;

	rc = __add_kek(pack, kek);
	if(rc) KEK_PACK_LOGE("%s failed. rc = %d", __func__, rc);

	return rc;
}

int del_kek(int userid, int kek_type) {
	kek_pack_t *pack;
	kek_item_t *item;

	KEK_PACK_LOGD("entered\n");

	pack = find_kek_pack(userid);
	if(pack == NULL) return -ENOENT;

	item = find_kek_item(pack, kek_type);
	if(item == NULL) return -ENOENT;

	spin_lock(&pack->kek_list_lock);
	del_kek_item(item);
	spin_unlock(&pack->kek_list_lock);

	return 0;
}

/*
 * I wanted to have some ref-count for kek_item_t that doesn't disturb
 * ongoing dek process. But the returned kek won't be zero-freed if the process
 * never returns.
 *
 * So allocate new memory and let the user call put accordingly
 */
kek_t *get_kek(int userid, int kek_type) {
	kek_pack_t *pack;
	kek_item_t *item;

	KEK_PACK_LOGD("entered\n");

	pack = find_kek_pack(userid);
	if(pack == NULL) return NULL;

	item = find_kek_item(pack, kek_type);
	if(item) {
		kek_t *kek = kmalloc(sizeof(kek_t), GFP_KERNEL);
		if(kek == NULL) return NULL;

		memcpy(kek, &item->kek, sizeof(kek_t));
		return kek;
	}

	return NULL;
}

void put_kek(kek_t *kek) {
	KEK_PACK_LOGD("entered\n");

	kzfree(kek);
}

int is_kek_pack(int userid) {
	kek_pack_t *pack;

	KEK_PACK_LOGD("entered\n");

	pack = find_kek_pack(userid);
	if(pack) return 1;

	return 0;
}

int is_kek(int userid, int kek_type) {
	kek_pack_t *pack;
	kek_item_t *item;

	KEK_PACK_LOGD("entered\n");

	pack = find_kek_pack(userid);
	if(pack == NULL) return 0;

	item = find_kek_item(pack, kek_type);
	if(item) {
		return 1;
	}

	return 0;
}
