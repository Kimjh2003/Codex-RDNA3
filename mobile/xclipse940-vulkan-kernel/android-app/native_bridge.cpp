#include <jni.h>

#include <string>

extern int xclipse_demo_main(int argc, char** argv);

extern "C" JNIEXPORT jint JNICALL
Java_dev_kimjh_xclipsegpu_MainActivity_runNative(
    JNIEnv* env, jobject, jstring astc, jstring shader, jstring golden,
    jstring out_rgba, jstring out_words) {
    const jstring args[] = {astc, shader, golden, out_rgba, out_words};
    std::string paths[5];
    for (int i = 0; i < 5; ++i) {
        const char* value = env->GetStringUTFChars(args[i], nullptr);
        if (!value) return 1;
        paths[i] = value;
        env->ReleaseStringUTFChars(args[i], value);
    }
    char name[] = "xclipse_demo";
    char* argv[] = {name, paths[0].data(), paths[1].data(), paths[2].data(),
                    paths[3].data(), paths[4].data()};
    return xclipse_demo_main(6, argv);
}
