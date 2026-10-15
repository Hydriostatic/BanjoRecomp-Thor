// HTTP on Android.
//
// The mod is loaded into the game's process by the mod loader, so it can't add Java code of its
// own. It finds the process's JavaVM through SDL (which the game already loaded) and calls Java
// classes over JNI. There are two ways to reach the server:
//
// - Bridge: the Banjo ports don't request android.permission.INTERNET, so the game process can't
//   open sockets at all. The RA Bridge companion app (bridge-app/) has the permission. It launches
//   the game with a Binder in the launch intent, and requests are sent through that Binder; the
//   bridge makes the HTTP request and sends the response back.
// - Direct: if the app does have internet access, java.net.HttpURLConnection is used in-process.

#if defined(__ANDROID__)

#include "http.h"

#include <dlfcn.h>
#include <jni.h>

#include <rc_api_request.h>

#include <thread>
#include <vector>

namespace {

JavaVM* java_vm = nullptr;

// The bridge's Binder, when the game was launched by RA Bridge. Must match BridgeBinder.java.
constexpr const char* bridge_extra_key = "io.github.hydriostatic.rabridge.BINDER";
constexpr const char* bridge_interface = "io.github.hydriostatic.rabridge.IHttpBridge";
constexpr jint bridge_transaction_http = 1; // IBinder.FIRST_CALL_TRANSACTION
jobject bridge_binder = nullptr;

struct BridgeClasses {
    jclass parcel;
    jclass binder;
    jmethodID parcel_obtain;
    jmethodID parcel_write_interface_token;
    jmethodID parcel_write_string;
    jmethodID parcel_write_byte_array;
    jmethodID parcel_read_exception;
    jmethodID parcel_read_int;
    jmethodID parcel_create_byte_array;
    jmethodID parcel_recycle;
    jmethodID binder_transact;
} bridge;

struct JavaClasses {
    jclass url;
    jclass http_connection;
    jclass url_connection;
    jclass output_stream;
    jclass input_stream;

    jmethodID url_init;
    jmethodID url_open_connection;
    jmethodID set_request_method;
    jmethodID set_connect_timeout;
    jmethodID set_read_timeout;
    jmethodID set_request_property;
    jmethodID set_do_output;
    jmethodID get_output_stream;
    jmethodID get_response_code;
    jmethodID get_input_stream;
    jmethodID get_error_stream;
    jmethodID disconnect;
    jmethodID output_write;
    jmethodID output_close;
    jmethodID input_read;
    jmethodID input_close;
} java;

bool clear_exception(JNIEnv* env) {
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return true;
    }
    return false;
}

jclass global_class(JNIEnv* env, const char* name) {
    jclass local = env->FindClass(name);
    if (local == nullptr) {
        clear_exception(env);
        return nullptr;
    }
    jclass global = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    return global;
}

bool load_java_classes(JNIEnv* env) {
    java.url = global_class(env, "java/net/URL");
    java.http_connection = global_class(env, "java/net/HttpURLConnection");
    java.url_connection = global_class(env, "java/net/URLConnection");
    java.output_stream = global_class(env, "java/io/OutputStream");
    java.input_stream = global_class(env, "java/io/InputStream");
    if (!java.url || !java.http_connection || !java.url_connection || !java.output_stream || !java.input_stream) {
        return false;
    }

    java.url_init = env->GetMethodID(java.url, "<init>", "(Ljava/lang/String;)V");
    java.url_open_connection = env->GetMethodID(java.url, "openConnection", "()Ljava/net/URLConnection;");
    java.set_request_method = env->GetMethodID(java.http_connection, "setRequestMethod", "(Ljava/lang/String;)V");
    java.set_connect_timeout = env->GetMethodID(java.url_connection, "setConnectTimeout", "(I)V");
    java.set_read_timeout = env->GetMethodID(java.url_connection, "setReadTimeout", "(I)V");
    java.set_request_property = env->GetMethodID(java.url_connection, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
    java.set_do_output = env->GetMethodID(java.url_connection, "setDoOutput", "(Z)V");
    java.get_output_stream = env->GetMethodID(java.url_connection, "getOutputStream", "()Ljava/io/OutputStream;");
    java.get_response_code = env->GetMethodID(java.http_connection, "getResponseCode", "()I");
    java.get_input_stream = env->GetMethodID(java.url_connection, "getInputStream", "()Ljava/io/InputStream;");
    java.get_error_stream = env->GetMethodID(java.http_connection, "getErrorStream", "()Ljava/io/InputStream;");
    java.disconnect = env->GetMethodID(java.http_connection, "disconnect", "()V");
    java.output_write = env->GetMethodID(java.output_stream, "write", "([B)V");
    java.output_close = env->GetMethodID(java.output_stream, "close", "()V");
    java.input_read = env->GetMethodID(java.input_stream, "read", "([B)I");
    java.input_close = env->GetMethodID(java.input_stream, "close", "()V");

    return !clear_exception(env);
}

// Whether the app's manifest grants android.permission.INTERNET. Without it every socket fails.
bool has_internet_permission(JNIEnv* env, jobject activity) {
    jclass context_class = env->GetObjectClass(activity);
    jmethodID check = env->GetMethodID(context_class, "checkSelfPermission", "(Ljava/lang/String;)I");
    if (check == nullptr) {
        clear_exception(env);
        env->DeleteLocalRef(context_class);
        return true; // can't tell; let the requests fail if it's missing
    }
    jstring permission = env->NewStringUTF("android.permission.INTERNET");
    jint result = env->CallIntMethod(activity, check, permission);
    bool failed = clear_exception(env);
    env->DeleteLocalRef(permission);
    env->DeleteLocalRef(context_class);
    return failed || result == 0; // PackageManager.PERMISSION_GRANTED
}

void set_property(JNIEnv* env, jobject connection, const char* key, const std::string& value) {
    jstring jkey = env->NewStringUTF(key);
    jstring jvalue = env->NewStringUTF(value.c_str());
    env->CallVoidMethod(connection, java.set_request_property, jkey, jvalue);
    env->DeleteLocalRef(jkey);
    env->DeleteLocalRef(jvalue);
}

std::string read_all(JNIEnv* env, jobject stream) {
    std::string out;
    jbyteArray buffer = env->NewByteArray(16384);
    while (true) {
        jint count = env->CallIntMethod(stream, java.input_read, buffer);
        if (clear_exception(env) || count < 0) {
            break;
        }
        size_t old_size = out.size();
        out.resize(old_size + count);
        env->GetByteArrayRegion(buffer, 0, count, reinterpret_cast<jbyte*>(out.data() + old_size));
    }
    env->CallVoidMethod(stream, java.input_close);
    clear_exception(env);
    env->DeleteLocalRef(buffer);
    return out;
}

ra_http::Response perform(JNIEnv* env, const ra_http::Request& request) {
    ra_http::Response response;
    response.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;

    if (env->PushLocalFrame(32) != JNI_OK) {
        clear_exception(env);
        return response;
    }

    jstring url_string = env->NewStringUTF(request.url.c_str());
    jobject url = env->NewObject(java.url, java.url_init, url_string);
    if (clear_exception(env) || url == nullptr) {
        response.status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        env->PopLocalFrame(nullptr);
        return response;
    }

    jobject connection = env->CallObjectMethod(url, java.url_open_connection);
    if (clear_exception(env) || connection == nullptr) {
        env->PopLocalFrame(nullptr);
        return response;
    }

    bool post = !request.post_data.empty();
    jstring method = env->NewStringUTF(post ? "POST" : "GET");
    env->CallVoidMethod(connection, java.set_request_method, method);
    env->CallVoidMethod(connection, java.set_connect_timeout, (jint)15000);
    env->CallVoidMethod(connection, java.set_read_timeout, (jint)30000);
    set_property(env, connection, "User-Agent", request.user_agent);
    clear_exception(env);

    if (post) {
        set_property(env, connection, "Content-Type",
            request.content_type.empty() ? "application/x-www-form-urlencoded" : request.content_type);
        env->CallVoidMethod(connection, java.set_do_output, JNI_TRUE);

        jobject output = env->CallObjectMethod(connection, java.get_output_stream);
        if (clear_exception(env) || output == nullptr) {
            env->CallVoidMethod(connection, java.disconnect);
            clear_exception(env);
            env->PopLocalFrame(nullptr);
            return response;
        }
        jbyteArray bytes = env->NewByteArray((jsize)request.post_data.size());
        env->SetByteArrayRegion(bytes, 0, (jsize)request.post_data.size(), reinterpret_cast<const jbyte*>(request.post_data.data()));
        env->CallVoidMethod(output, java.output_write, bytes);
        env->CallVoidMethod(output, java.output_close);
        if (clear_exception(env)) {
            env->CallVoidMethod(connection, java.disconnect);
            clear_exception(env);
            env->PopLocalFrame(nullptr);
            return response;
        }
    }

    jint status = env->CallIntMethod(connection, java.get_response_code);
    if (clear_exception(env)) {
        env->CallVoidMethod(connection, java.disconnect);
        clear_exception(env);
        env->PopLocalFrame(nullptr);
        return response;
    }

    jobject input = env->CallObjectMethod(connection, java.get_input_stream);
    if (clear_exception(env) || input == nullptr) {
        input = env->CallObjectMethod(connection, java.get_error_stream);
        clear_exception(env);
    }
    if (input != nullptr) {
        response.body = read_all(env, input);
    }

    env->CallVoidMethod(connection, java.disconnect);
    clear_exception(env);
    env->PopLocalFrame(nullptr);

    response.status = status;
    return response;
}

// Looks for the bridge's Binder in the intent that launched the game's activity.
jobject find_bridge_binder(JNIEnv* env, jobject activity) {
    jobject result = nullptr;
    if (env->PushLocalFrame(16) != JNI_OK) {
        clear_exception(env);
        return nullptr;
    }

    jclass activity_class = env->GetObjectClass(activity);
    jmethodID get_intent = env->GetMethodID(activity_class, "getIntent", "()Landroid/content/Intent;");
    jobject intent = get_intent ? env->CallObjectMethod(activity, get_intent) : nullptr;
    if (!clear_exception(env) && intent != nullptr) {
        jclass intent_class = env->GetObjectClass(intent);
        jmethodID get_extras = env->GetMethodID(intent_class, "getExtras", "()Landroid/os/Bundle;");
        jobject extras = get_extras ? env->CallObjectMethod(intent, get_extras) : nullptr;
        if (!clear_exception(env) && extras != nullptr) {
            jclass bundle_class = env->GetObjectClass(extras);
            jmethodID get_binder = env->GetMethodID(bundle_class, "getBinder", "(Ljava/lang/String;)Landroid/os/IBinder;");
            jstring key = env->NewStringUTF(bridge_extra_key);
            jobject binder = get_binder ? env->CallObjectMethod(extras, get_binder, key) : nullptr;
            if (!clear_exception(env) && binder != nullptr) {
                result = env->NewGlobalRef(binder);
            }
        }
    }
    clear_exception(env);

    env->PopLocalFrame(nullptr);
    return result;
}

bool load_bridge_classes(JNIEnv* env) {
    bridge.parcel = global_class(env, "android/os/Parcel");
    bridge.binder = global_class(env, "android/os/IBinder");
    if (!bridge.parcel || !bridge.binder) {
        return false;
    }
    bridge.parcel_obtain = env->GetStaticMethodID(bridge.parcel, "obtain", "()Landroid/os/Parcel;");
    bridge.parcel_write_interface_token = env->GetMethodID(bridge.parcel, "writeInterfaceToken", "(Ljava/lang/String;)V");
    bridge.parcel_write_string = env->GetMethodID(bridge.parcel, "writeString", "(Ljava/lang/String;)V");
    bridge.parcel_write_byte_array = env->GetMethodID(bridge.parcel, "writeByteArray", "([B)V");
    bridge.parcel_read_exception = env->GetMethodID(bridge.parcel, "readException", "()V");
    bridge.parcel_read_int = env->GetMethodID(bridge.parcel, "readInt", "()I");
    bridge.parcel_create_byte_array = env->GetMethodID(bridge.parcel, "createByteArray", "()[B");
    bridge.parcel_recycle = env->GetMethodID(bridge.parcel, "recycle", "()V");
    bridge.binder_transact = env->GetMethodID(bridge.binder, "transact", "(ILandroid/os/Parcel;Landroid/os/Parcel;I)Z");
    return !clear_exception(env);
}

jbyteArray to_byte_array(JNIEnv* env, const std::string& value) {
    jbyteArray bytes = env->NewByteArray((jsize)value.size());
    env->SetByteArrayRegion(bytes, 0, (jsize)value.size(), reinterpret_cast<const jbyte*>(value.data()));
    return bytes;
}

// Sends the request to RA Bridge. Request parcel: interface token, url, post data (byte[]),
// content type, user agent. Reply: no exception, status (int), body (byte[]).
ra_http::Response perform_bridge(JNIEnv* env, const ra_http::Request& request) {
    ra_http::Response response;
    response.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;

    if (env->PushLocalFrame(32) != JNI_OK) {
        clear_exception(env);
        return response;
    }

    jobject data = env->CallStaticObjectMethod(bridge.parcel, bridge.parcel_obtain);
    jobject reply = env->CallStaticObjectMethod(bridge.parcel, bridge.parcel_obtain);
    if (clear_exception(env) || data == nullptr || reply == nullptr) {
        env->PopLocalFrame(nullptr);
        return response;
    }

    env->CallVoidMethod(data, bridge.parcel_write_interface_token, env->NewStringUTF(bridge_interface));
    env->CallVoidMethod(data, bridge.parcel_write_string, env->NewStringUTF(request.url.c_str()));
    env->CallVoidMethod(data, bridge.parcel_write_byte_array, to_byte_array(env, request.post_data));
    env->CallVoidMethod(data, bridge.parcel_write_string, env->NewStringUTF(request.content_type.c_str()));
    env->CallVoidMethod(data, bridge.parcel_write_string, env->NewStringUTF(request.user_agent.c_str()));

    jboolean sent = env->CallBooleanMethod(bridge_binder, bridge.binder_transact, bridge_transaction_http, data, reply, (jint)0);
    if (!clear_exception(env) && sent) {
        env->CallVoidMethod(reply, bridge.parcel_read_exception);
        if (!clear_exception(env)) {
            jint status = env->CallIntMethod(reply, bridge.parcel_read_int);
            jbyteArray body = static_cast<jbyteArray>(env->CallObjectMethod(reply, bridge.parcel_create_byte_array));
            if (!clear_exception(env)) {
                if (body != nullptr) {
                    jsize length = env->GetArrayLength(body);
                    response.body.resize(length);
                    env->GetByteArrayRegion(body, 0, length, reinterpret_cast<jbyte*>(response.body.data()));
                }
                response.status = status;
            }
        }
    }

    env->CallVoidMethod(data, bridge.parcel_recycle);
    env->CallVoidMethod(reply, bridge.parcel_recycle);
    clear_exception(env);
    env->PopLocalFrame(nullptr);
    return response;
}

}

bool ra_http::init(std::string& error) {
    void* sdl = dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD);
    if (sdl == nullptr) {
        error = "SDL isn't loaded in this process";
        return false;
    }
    using GetJNIEnv = void* (*)();
    using GetActivity = void* (*)();
    auto get_env = reinterpret_cast<GetJNIEnv>(dlsym(sdl, "SDL_AndroidGetJNIEnv"));
    auto get_activity = reinterpret_cast<GetActivity>(dlsym(sdl, "SDL_AndroidGetActivity"));
    if (get_env == nullptr) {
        error = "SDL_AndroidGetJNIEnv not found";
        return false;
    }

    JNIEnv* env = static_cast<JNIEnv*>(get_env());
    if (env == nullptr || env->GetJavaVM(&java_vm) != JNI_OK) {
        error = "couldn't reach the Java VM";
        return false;
    }

    jobject activity = get_activity != nullptr ? static_cast<jobject>(get_activity()) : nullptr;
    if (activity == nullptr) {
        error = "couldn't find the game's activity";
        return false;
    }
    bool internet = has_internet_permission(env, activity);
    jobject binder = find_bridge_binder(env, activity);
    env->DeleteLocalRef(activity);

    // Prefer the bridge when the game was launched through it.
    if (binder != nullptr) {
        if (!load_bridge_classes(env)) {
            env->DeleteGlobalRef(binder);
            error = "couldn't load the Binder classes";
            return false;
        }
        bridge_binder = binder;
        return true;
    }

    if (internet) {
        if (!load_java_classes(env)) {
            error = "couldn't load java.net classes";
            return false;
        }
        return true;
    }

    error = "this app has no internet access. Close the game and start it from the RA Bridge app";
    return false;
}

bool ra_http::using_bridge() {
    return bridge_binder != nullptr;
}

void ra_http::send_async(Request request, std::function<void(Response)> done) {
    std::thread([request = std::move(request), done = std::move(done)]() {
        Response response;
        response.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;

        JNIEnv* env = nullptr;
        if (java_vm != nullptr && java_vm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            response = bridge_binder != nullptr ? perform_bridge(env, request) : perform(env, request);
            java_vm->DetachCurrentThread();
        }

        done(std::move(response));
    }).detach();
}

#endif
