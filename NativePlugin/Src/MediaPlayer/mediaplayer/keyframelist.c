#include "stdefine.h"
#include"keyframelist.h"
#include <stdlib.h> 
#include <math.h>
#include <stdio.h>
// int main(void)
// {
// 	KEYFRAMELIST arr;
// 	init(&arr, 1);
// 	printf("init");
// 	toString(&arr);
// 	int i = 0;
// 	for (; i < 10; i++)
// 	{
// 		append(&arr, i);
// 	}
	
// 	toString(&arr);

// 	insert(&arr, 1, 22);
// 	printf("insert:1,22");
// 	toString(&arr);

// 	insert(&arr, 3, 55);
// 	printf("insert:3,55");
// 	toString(&arr);

// 	del(&arr, 2);
// 	printf("del:2");
// 	toString(&arr);

// 	reversion(&arr);
// 	printf("reversion");
// 	toString(&arr);

// 	sort(&arr);
// 	printf("sort");
// 	toString(&arr);

	
// 	return 0;
// }


void keyframe_list_init(KEYFRAMELIST *pArr, int size){
	pArr->pElements = (int64_t *)malloc(sizeof(int64_t)* size);
	pArr->key = 0;
	pArr->size = size;
}

void keyframe_list_destory(KEYFRAMELIST *pArr){
    free(pArr->pElements);
    pArr->pElements = 0;
	pArr->key = 0;
	pArr->size = 0;
}

int keyframe_list_append(KEYFRAMELIST *pArr, int64_t val){
	if (keyframe_list_full(pArr)) {
		keyframe_list_resize(pArr);
	}
	pArr->pElements[pArr->key] = val;
	pArr->key++;
	return 1;
}

//动态扩容
void keyframe_list_resize(KEYFRAMELIST *pArr)
{
	if (pArr->key >= pArr->size)
	{
		int *newArr;
		int oldSize = (pArr->size == 1) ? 2 : pArr->size;
		int newSize = ceil(oldSize + (oldSize >> 1)); //1.5倍
//		printf("%d \n", newSize);
		newArr = malloc(sizeof(newArr) * newSize);
		int i = 0;
		for (; i <= pArr->key; i++)
		{
			newArr[i] = pArr->pElements[i];
		}
		free(pArr->pElements);
		pArr->pElements = (int64_t*)newArr;
		pArr->size = newSize;
	}
}

int keyframe_list_insert(KEYFRAMELIST *pArr, int index, int64_t val){
	int i;
	if (keyframe_list_full(pArr)) return 0;
	
	for (i = pArr->key; i >index; --i)
	{
		pArr->pElements[i] = pArr->pElements[i-1];
	}
	pArr->pElements[index] = val;
	pArr->key++;
	return 1;
}

int keyframe_list_del(KEYFRAMELIST *pArr, int index){
	int i;
	if (keyframe_list_empty(pArr)) return 0;
	
	for (i = index; i <pArr->key; ++i)
	{
		pArr->pElements[i] = pArr->pElements[i+1];
	}	
	pArr->key--;
	return 1;
}

void keyframe_list_reversion(KEYFRAMELIST *pArr){
	if (keyframe_list_empty(pArr)) return;
	int i = 0;
	int j = pArr->key-1;
	int t;
	while (i < j)
	{
		t = pArr->pElements[i];
		pArr->pElements[i] = pArr->pElements[j];
		pArr->pElements[j] = t;
		++i;
		--j;
	}
}

void keyframe_list_sort(KEYFRAMELIST *pArr){
	if (keyframe_list_empty(pArr)) return;
	int i,j,t;

	for (i = 0; i < pArr->key; ++i)
	{
		for (j = i; j < pArr->key; ++j)
		{
			if (pArr->pElements[i] > pArr->pElements[j]){
				t = pArr->pElements[j];
				pArr->pElements[j] = pArr->pElements[i];
				pArr->pElements[i] = t;
			}
		}
	}
}

int64_t keyframe_list_get(KEYFRAMELIST *pArr,int index){
    if (keyframe_list_empty(pArr)) return 0;
    if(pArr->key < index) return 0;
    return pArr->pElements[index];
}

void keyframe_list_toString(KEYFRAMELIST *pArr){
	int i;
	if (keyframe_list_empty(pArr))
	{
		printf("[] \n");
		return;
	}
	printf("[");
	for (i = 0; i < pArr->key; i++)
	{
		printf("%ld ,", pArr->pElements[i]);
	}
	printf("] \n");
	return;
}


int keyframe_list_empty(KEYFRAMELIST *pArr){
	if (pArr->key > 0) return 0;
	else return 1;
}

int keyframe_list_full(KEYFRAMELIST *pArr){
	if (pArr->key >= pArr->size)
		return 1;
	else
		return 0;
}