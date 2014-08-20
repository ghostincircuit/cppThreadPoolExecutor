#include <jni.h>
#include "com_example_testthreadpoolexecutor_MainActivity.h"

JNIEXPORT void JNICALL
Java_com_example_testthreadpoolexecutor_MainActivity_jni(JNIEnv *e, jobject o)
{
	extern int tmain();
	tmain();
}
