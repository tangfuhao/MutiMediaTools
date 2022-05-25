#ifndef __KEYFRAME_QUEUE_H__
#define __KEYFRAME_QUEUE_H__


#ifdef __cplusplus
extern "C" {
#endif


typedef struct _keyfram_array
{
	int size; //array大小
	int key;  //最在元素key
	int64_t *pElements;
} KEYFRAMELIST;

void keyframe_list_init(KEYFRAMELIST *pArr, int size);
void keyframe_list_destory(KEYFRAMELIST *pArr);
void keyframe_list_resize(KEYFRAMELIST *pArr);
void keyframe_list_toString(KEYFRAMELIST *pArr);
int keyframe_list_empty(KEYFRAMELIST *pArr);
int keyframe_list_full(KEYFRAMELIST *pArr);
int keyframe_list_append(KEYFRAMELIST *pArr, int64_t val);
int keyframe_list_insert(KEYFRAMELIST *pArr, int index, int64_t val);
int keyframe_list_del(KEYFRAMELIST *pArr, int index);
void keyframe_list_reversion(KEYFRAMELIST *pArr);
void keyframe_list_sort(KEYFRAMELIST *pArr);
int64_t keyframe_list_get(KEYFRAMELIST *pArr,int index);

#ifdef __cplusplus
}
#endif

#endif





