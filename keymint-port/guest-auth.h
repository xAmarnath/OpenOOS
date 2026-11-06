#ifndef GUEST_AUTH_H
#define GUEST_AUTH_H
#include <stdint.h>
#include <stddef.h>
#include <endian.h>
#include <time.h>
#include <sys/random.h>
static unsigned char guest_auth_key[32];
typedef struct __attribute__((packed)) {
    uint8_t version;
    uint64_t challenge,user_id,authenticator_id;
    uint32_t authenticator_type;
    uint64_t timestamp;
    uint8_t hmac[32];
} GuestAuthToken;
static int guest_auth_read(int dir) {
    int fd=openat(dir,".guest-auth-session",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st; int ok=fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&
        st.st_uid==1017&&(st.st_mode&077)==0&&st.st_size==32&&
        read(fd,guest_auth_key,32)==32;
    if(fd>=0)close(fd);return ok;
}
static int guest_auth_create(int dir) {
    if(getrandom(guest_auth_key,32,0)!=32)return 0;
    char tmp[64];snprintf(tmp,sizeof(tmp),".guest-auth-next-%ld",(long)getpid());
    int fd=openat(dir,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    const char *label="u:object_r:keystore_data_file:s0";
    if(fd<0)return 0;
    int ok=!fchown(fd,1017,1017)&&!fsetxattr(fd,"security.selinux",label,strlen(label)+1,0)&&
        write(fd,guest_auth_key,32)==32&&!fsync(fd);
    close(fd);
    if(ok)ok=!renameat(dir,tmp,dir,".guest-auth-session")&&!fsync(dir);
    if(!ok)unlinkat(dir,tmp,0);
    return ok;
}
static void guest_auth_sign(const void *data,size_t size,unsigned char out[32]) {
    GHmac *h=g_hmac_new(G_CHECKSUM_SHA256,guest_auth_key,32);
    g_hmac_update(h,(const guchar*)data,size);gsize n=32;g_hmac_get_digest(h,out,&n);g_hmac_unref(h);
}
/* Optional AIDL HAT. Never accept a token merely because AOSP's software
 * KeyMint mock accepts its signature. Only this run's guest verifier signs it. */
static int guest_auth_validate(GBinderReader *r,uint64_t max_age_ms) {
    GBinderReader t,stamp;gboolean present=0,has_stamp=0;
    if(!gbinder_reader_start_parcelable(r,&t,&present))return 0;
    if(!present)return 1; /* KeyMint still enforces token requirements. */
    GuestAuthToken token={0};gint64 c,u,a,ms;gint32 type;
    if(!gbinder_reader_read_int64(&t,&c)||!gbinder_reader_read_int64(&t,&u)||
       !gbinder_reader_read_int64(&t,&a)||!gbinder_reader_read_int32(&t,&type)||
       !gbinder_reader_start_parcelable(&t,&stamp,&has_stamp)||!has_stamp||
       !gbinder_reader_read_int64(&stamp,&ms))return 0;
    gbinder_reader_finish_parcelable(&stamp);
    gsize n=0;const void *mac=gbinder_reader_read_byte_array(&t,&n);
    if(!mac||n!=32||type!=1)return 0;
    token.challenge=c;token.user_id=u;token.authenticator_id=a;
    token.authenticator_type=htobe32(type);token.timestamp=htobe64(ms);
    unsigned char expected[32];guest_auth_sign(&token,offsetof(GuestAuthToken,hmac),expected);
    unsigned diff=0;for(int i=0;i<32;i++)diff|=expected[i]^((const unsigned char*)mac)[i];
    struct timespec now;if(clock_gettime(CLOCK_BOOTTIME,&now))return 0;
    uint64_t current=(uint64_t)now.tv_sec*1000+now.tv_nsec/1000000;
    /* Fresh-authentication guest fallback. Hardware-bound credentials are
     * deliberately not mixed with stock Gatekeeper or stock HMAC agreement. */
    if(diff||ms<0||(uint64_t)ms>current||current-(uint64_t)ms>max_age_ms)return 0;
    gbinder_reader_finish_parcelable(&t);return 1;
}
#endif
