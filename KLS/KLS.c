
#include <linux/types.h>
#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/slab.h>

#define MAX_KLS_SIZE 256

//We define the hashtable.
DEFINE_HASHTABLE(kls_table, 8);

//One key in the hashtable
struct key{
    //A pointer to the key string
    char* key;
    //The length of the key
    size_t keylen;
    //this struct grant us access to the next element in the same bucket of the hashtable
    struct hlist_node hash_list;
    //the header of the list which contain the different values of this key
    struct list_head keyListHead;
};

//Represent a value (identified with a key and an index)
struct value{
    //The string value
    char* val;
    //the length of the string
    size_t vallen;
    //this struct give us access to the next (and the previous) ellement of the list
    struct list_head val_list;
};

//A very basic function whick compare two string. Same as strcmp except that the two strings doesn't have to be null terminated
static int kls_strcmp(const char* string1, size_t length1,
                  const char* string2, size_t length2){
    size_t i = 0;
    if(length1 != length2)
        return 0;
    while(i < length1){
        if(string1[i] != string2[i])
            return 0;
        i++;
    }
    return 1;
}

//Copy the value of string1 in string2, same function as strcmpy except that the string to copy doesn't have to be null terminated
static void kls_strcpy(const char* string1, size_t length,
                  char* string2){
    size_t i = 0;
    while(i < length){
        string2[i] = string1[i];
        i++;
    }
}

//Add a given value of length vallen to the list head
static int kls_add_value(const char *val, size_t vallen, struct list_head* head){
    //Allocate the new value we will add to the list
    struct value* new_value = kmalloc(sizeof(struct value), GFP_KERNEL);
    //If allocation failed (we don't have enough memory space), kmalloc return NULL
    if(new_value == NULL)
            return -1;
    if(vallen < MAX_KLS_SIZE){
        //We allocate the val field before filling it
        new_value->val = kmalloc(vallen * sizeof(char), GFP_KERNEL);
        if(new_value->val == NULL){
            //If the allocation failed, we musst free the previous allocated value
            kfree(new_value);
            return -1;
        }
    }
    else
        return -1;
    //We musst initiate the val_list struct
    INIT_LIST_HEAD(&new_value->val_list);
    new_value->vallen = vallen;
    //We copy the value given in argument in the new_value
    kls_strcpy(val, vallen, new_value->val);
    //We add the new_value at the end of the list given as an argument
    list_add_tail(&new_value->val_list, head);
    return 0;
}

//A very simple algorithm to calculate the hash of a string. We're just going to sum the different characters
//This algorithm can generate the same hash several times for different functions but it has the advantage of being very fast
static int kls_hash_key(const char *val, size_t vallen){
    int i = 0;
    int sum = 0;
    while(i < vallen){
        sum += val[i];
        i++;
    }
    return sum;
}

asmlinkage long kls_insert(const char *key, size_t keylen,
                           const char *val, size_t vallen){
    struct key* current_key;
    struct key* new_key;
    int hash_key;
    //We check if the pointers given by the user does exist
    if(key == NULL){
        printk(KERN_ERR "Pointer Error : The given key pointer is not valid \n");
        return EFAULT;
    }
    if(val == NULL){
        printk(KERN_ERR "Pointer Error : The given value pointer is not valid \n");
        return EFAULT;
    }
    //We check if the keylen is valid
    if(keylen >= MAX_KLS_SIZE){
        printk(KERN_ERR "Size Error : Max key size is %d\n", MAX_KLS_SIZE);
        return EINVAL;
    }
    //We compute the value of the hash of the key
    hash_key = kls_hash_key(key, keylen);
    //We check if we find the key in the bucket containg the keys with the corresponding hash
    hash_for_each_possible(kls_table, current_key, hash_list, hash_key){
        //If we find the key
        if(kls_strcmp(current_key->key, current_key->keylen, key, keylen)){
            //We try to ass the value in the given key's list
            if(!kls_add_value(val, vallen, &current_key->keyListHead))
                return 0;
            //If we failed at adding the value, it means we've a memory problem so we send the corresponding error
            printk(KERN_ERR "Memory Error : Out of memory, impossible to create new value");
            return ENOMEM;
        }
    }
    //If we can't find the key in the corresponding buccket, we allocate it
    new_key = kmalloc(sizeof(struct key), GFP_KERNEL);
    if(new_key == NULL){
        printk(KERN_ERR "Memory Error : Out of memory, impossible to create new key");
        return ENOMEM;
    }
    //We allocate a table for storing the new key
    new_key->key = kmalloc(keylen * sizeof(char), GFP_KERNEL);
    if(new_key->key == NULL){
        //If it fail, we free the prefious allocated key
        kfree(new_key);
        printk(KERN_ERR "Memory Error : Out of memory, impossible to create new key");
        return ENOMEM;
    }
    //We copy the new key value in the new_key key field
    kls_strcpy(key, keylen, new_key->key);
    new_key->keylen = keylen;
    //We initialise the keyListHead (which is the list of the value stored at the given key)
    INIT_LIST_HEAD(&new_key->keyListHead);
    //We then store the new key in the global hash table
    hash_add(kls_table, &new_key->hash_list, hash_key);
    if(!kls_add_value(val, vallen, &new_key->keyListHead))
        return 0;
    //If we can't add the value at the given key, it means we have a memory problem
    //We remove the new_key from the hash table
    hash_del(&new_key->hash_list);
    //We free the new key's field
    kfree(new_key->key);
    //we free the new key
    kfree(new_key);
    printk(KERN_ERR "Memory Error : Out of memory, impossible to create new value");
    return ENOMEM;
}

asmlinkage long kls_search(const char *key, size_t keylen,
                            char *val, size_t index){
    int i = 0;
    int hash_key;
    struct value *current_value = NULL;
    struct key* current_key;
    if(key == NULL){
        printk(KERN_ERR "Pointer Error : The given key pointer is not valid \n");
        return EFAULT;
    }
    if(val == NULL){
        printk(KERN_ERR "Pointer Error : The given value pointer is not valid \n");
        return EFAULT;
    }
    if(keylen >= MAX_KLS_SIZE){
        printk(KERN_ERR "Size Error : Max key size is %d\n", MAX_KLS_SIZE);
        return EINVAL;
    }
    hash_key = kls_hash_key(key, keylen);
    //We will search for the key in the bucket given by the hash
    hash_for_each_possible(kls_table, current_key, hash_list, hash_key){
        //If we find the key, we will search for the value at the given index (the list of all the values of a key are in the keyListHead)
        if(kls_strcmp(current_key->key, current_key->keylen, key, keylen)){
            //We will loop throught all the values in the list
            list_for_each_entry(current_value, &current_key->keyListHead, val_list){
                //At each iteration, we will add one to i (which is initialy 0) and if at any time, i is equal to the index we requested for, we found our value !
                if(i == index){
                    //We copy the value in the given buffer
                    kls_strcpy(current_value->val, current_value->vallen, val);
                    return 0;
                }
                i++;
            }
            //If we end the list_for_each loop, it means the value at the given index doesn't exist.
            printk(KERN_ERR "Index Error : Index max is %d\n", i);
            return EINVAL;
        }
    }
    //If we found any value with the key we are searching for in the bucket with the corresponding hash, the the key does not exist
    printk(KERN_ERR "Key Error : Key not found");
    return ENOENT;
}

asmlinkage long kls_delete(const char *key, size_t keylen){
    int hash_key;
    struct value *current_value = NULL;
    struct key* current_key;
    if(key == NULL){
        printk(KERN_ERR "Pointer Error : The given key pointer is not valid \n");
        return EFAULT;
    }
    if(keylen >= MAX_KLS_SIZE){
        printk(KERN_ERR "Size Error : Max key size is %d\n", MAX_KLS_SIZE);
        return EINVAL;
    }
    hash_key = kls_hash_key(key, keylen);
    //Like we did it before, we search for the key in the good bucket
    hash_for_each_possible(kls_table, current_key, hash_list, hash_key){
        //If we find it
        if(kls_strcmp(current_key->key, current_key->keylen, key, keylen)){
            //We will delete some value until all the value stored in the given key are removed
            while(!list_empty(&current_key->keyListHead)){
                //we take the first value of the list (stored just after the keyListHead)
                current_value = list_entry(current_key->keyListHead.next, struct value, val_list);
                //We free the value field of the current value
                kfree(current_value->val);
                //We make the keyListHead next value point to the value after the value struct we will remove
                current_key->keyListHead.next = current_value->val_list.next;
                //We finally free the value previously following keyListHead
                kfree(current_value);
                //This loop will continue until current_key->keyListHead.next point to itself (meaning all the list have been freed)
            }
            //We delete the key from the hashtable
            hash_del(&current_key->hash_list);
            //We free the key field of the key we are working with
            kfree(current_key->key);
            //We finally free the key structure
            kfree(current_key);
            return 0;
        }
    }
    //If we don't find the key in the hashtable, it means it doesn't exist
    printk(KERN_ERR "Key Error : key not found");
    return ENOENT;
}