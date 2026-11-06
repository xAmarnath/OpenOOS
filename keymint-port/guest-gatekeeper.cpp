// Guest-only Gatekeeper using the AOSP verification/throttling core.
#include <gbinder.h>
#include <glib-unix.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>
#include <gatekeeper/gatekeeper.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "guest-fd-hygiene.h"
#include "guest-auth.h"
#define BASE "/data/local/tmp/gsi17-graphics-20261105-01"
#define IFACE "android.hardware.gatekeeper.IGatekeeper"
using namespace gatekeeper;
static int records=-1;
static bool atomic_record(uint32_t uid,const failure_record_t *r) {
    char name[40],tmp[64];snprintf(name,sizeof(name),"failure-%u",uid);snprintf(tmp,sizeof(tmp),".next-%u-%ld",uid,(long)getpid());
    int fd=openat(records,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return false;
    bool ok=write(fd,r,sizeof(*r))==sizeof(*r)&&!fsync(fd);close(fd);
    if(ok)ok=!renameat(records,tmp,records,name)&&!fsync(records);
    if(!ok)unlinkat(records,tmp,0);return ok;
}
class GuestGateKeeper:public GateKeeper {
    unsigned char password_key[32]={0};
public:
    bool GetAuthTokenKey(const uint8_t **key,uint32_t *len)const override{*key=guest_auth_key;*len=32;return true;}
    void GetPasswordKey(const uint8_t **key,uint32_t *len)override{*key=password_key;*len=32;}
    void ComputePasswordSignature(uint8_t *sig,uint32_t n,const uint8_t*,uint32_t,const uint8_t *pw,uint32_t pn,salt_t salt)const override{
        if(EVP_PBE_scrypt((const char*)pw,pn,(const unsigned char*)&salt,sizeof(salt),16384,8,1,64*1024*1024,sig,n)!=1){
            fputs("scrypt failed; refusing credentials\n",stderr);_exit(70);
        }
    }
    void GetRandom(void *data,uint32_t n)const override{if(getrandom(data,n,0)!=n)_exit(71);}
    void ComputeSignature(uint8_t *sig,uint32_t n,const uint8_t *key,uint32_t kn,const uint8_t *msg,uint32_t mn)const override{
        GHmac *h=g_hmac_new(G_CHECKSUM_SHA256,key,kn);g_hmac_update(h,msg,mn);gsize size=n;g_hmac_get_digest(h,sig,&size);g_hmac_unref(h);
    }
    uint64_t GetMillisecondsSinceBoot()const override{timespec t;if(clock_gettime(CLOCK_BOOTTIME,&t))_exit(72);return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
    bool IsHardwareBacked()const override{return false;}
    bool GetFailureRecord(uint32_t uid,secure_id_t sid,failure_record_t *r,bool secure)override{
        if(secure)return false;
        char name[40];snprintf(name,sizeof(name),"failure-%u",uid);
        int fd=openat(records,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        if(fd<0){if(errno!=ENOENT)return false;*r={sid,0,0};return true;}
        struct stat st;bool ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==sizeof(*r)&&read(fd,r,sizeof(*r))==sizeof(*r);close(fd);
        if(ok&&r->secure_user_id!=sid)*r={sid,0,0};return ok;
    }
    bool ClearFailureRecord(uint32_t uid,secure_id_t sid,bool secure)override{
        if(secure)return false;failure_record_t r={sid,0,0};return atomic_record(uid,&r);
    }
    bool WriteFailureRecord(uint32_t uid,failure_record_t *r,bool secure)override{return !secure&&atomic_record(uid,r);}
};
static GuestGateKeeper impl;
static GBinderLocalReply *error(GBinderLocalObject *o,int e){
    auto q=gbinder_local_object_new_reply(o);GBinderWriter w;gbinder_local_reply_init_writer(q,&w);
    gbinder_writer_append_int32(&w,-8);gbinder_writer_append_string16(&w,"guest credential service");
    gbinder_writer_append_int32(&w,0);gbinder_writer_append_int32(&w,e);return q;
}
static SizedBuffer bytes(GBinderReader *r){
    gsize n=0;const void *p=gbinder_reader_read_byte_array(r,&n);if(!p||!n||n>4096)return {};
    auto b=new uint8_t[n];memcpy(b,p,n);return {b,(uint32_t)n};
}
static GBinderLocalReply *dispatch(GBinderLocalObject *o,GBinderRemoteRequest *req,guint code,guint flags,int *status,void*){
    *status=0;auto uid=gbinder_remote_request_sender_euid(req);
    if((uid!=0&&uid!=1000)||flags||strcmp(gbinder_remote_request_interface(req),IFACE))return error(o,-1);
    if(code==1||code==2)return error(o,-3); // Optional deletes; never forward.
    if(code!=3&&code!=4&&code!=0xffffff&&code!=0xfffffe)return error(o,-3);
    GBinderReader r;gbinder_remote_request_init_reader(req,&r);
    if(gbinder_reader_bytes_remaining(&r)>16384)return error(o,-1);
    auto q=gbinder_local_object_new_reply(o);GBinderWriter w,p;gbinder_local_reply_init_writer(q,&w);gbinder_writer_append_int32(&w,0);
    if(code==0xffffff){gbinder_writer_append_int32(&w,1);return q;}
    if(code==0xfffffe){gbinder_writer_append_string16(&w,"notfrozen");return q;}
    gint32 user;gint64 challenge=0;
    if(!gbinder_reader_read_int32(&r,&user)||user<0)goto bad;
    if(code==4&&!gbinder_reader_read_int64(&r,&challenge))goto bad;
    {
        auto handle=bytes(&r),pw=bytes(&r);
        if((handle.size()&&handle.size()!=sizeof(password_handle_t))||(code==4&&!handle))goto bad;
        int ec=0,timeout=0;EnrollResponse enrolled;VerifyResponse verified;
        if(code==3){
            auto desired=bytes(&r);if(!desired||!gbinder_reader_at_end(&r))goto bad;
            EnrollRequest request(user,gatekeeper::move(handle),gatekeeper::move(desired),gatekeeper::move(pw));impl.Enroll(request,&enrolled);
            ec=enrolled.error;if(ec==ERROR_RETRY)timeout=enrolled.retry_timeout;
        }else{
            if(!pw||!gbinder_reader_at_end(&r))goto bad;
            VerifyRequest request(user,challenge,gatekeeper::move(handle),gatekeeper::move(pw));impl.Verify(request,&verified);
            ec=verified.error;if(ec==ERROR_RETRY)timeout=verified.retry_timeout;
        }
        fprintf(stderr,"guest credential method=%u result=%d\n",code,ec);
        if(ec!=ERROR_NONE&&ec!=ERROR_RETRY)goto bad;
        gbinder_writer_start_parcelable(&w,&p);
        gbinder_writer_append_int32(&p,ec==ERROR_RETRY?-2:(code==4&&verified.request_reenroll?1:0));
        gbinder_writer_append_int32(&p,ec==ERROR_RETRY?timeout:0);
        if(code==3){
            const auto *h=enrolled.enrolled_password_handle.Data<password_handle_t>();
            gbinder_writer_append_int64(&p,h?h->user_id:0);
            gbinder_writer_append_byte_array(&p,enrolled.enrolled_password_handle.Data<uint8_t>(),enrolled.enrolled_password_handle.size());
        }else{
            const auto *token=verified.auth_token.Data<hw_auth_token_t>();
            GBinderWriter t,ts;gbinder_writer_start_parcelable(&p,&t);
            gbinder_writer_append_int64(&t,token?token->challenge:0);gbinder_writer_append_int64(&t,token?token->user_id:0);
            gbinder_writer_append_int64(&t,token?token->authenticator_id:0);gbinder_writer_append_int32(&t,token?be32toh(token->authenticator_type):0);
            gbinder_writer_start_parcelable(&t,&ts);gbinder_writer_append_int64(&ts,token?be64toh(token->timestamp):0);gbinder_writer_finish_parcelable(&ts);
            gbinder_writer_append_byte_array(&t,token?token->hmac:nullptr,token?32:0);gbinder_writer_finish_parcelable(&t);
        }
        gbinder_writer_finish_parcelable(&p);return q;
    }
bad:gbinder_local_reply_unref(q);return error(o,-1);
}
static SizedBuffer copybuf(const void *p,size_t n){auto b=new uint8_t[n];memcpy(b,p,n);return {b,(uint32_t)n};}
static SizedBuffer copybuf(const SizedBuffer &b){return copybuf(b.Data<uint8_t>(),b.size());}
static int selftest(const char *path){
    const char *prefix=BASE"/gatekeeper-selftest-";
    if(strncmp(path,prefix,strlen(prefix))||!path[strlen(prefix)])return 80;
    for(const char *p=path+strlen(prefix);*p;p++)if(*p<'0'||*p>'9')return 80;
    if(mkdir(path,0700)||(records=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC))<0)return 81;
    if(getrandom(guest_auth_key,32,0)!=32)return 82;
    const char pass[]="isolated-credential-test",wrong[]="incorrect",next[]="new-isolated-credential";
    EnrollResponse enrolled;EnrollRequest e(777001,{},copybuf(pass,sizeof(pass)),{});impl.Enroll(e,&enrolled);
    if(enrolled.error||!enrolled.enrolled_password_handle)return 83;
    auto handle=copybuf(enrolled.enrolled_password_handle);
    VerifyResponse good;VerifyRequest v(777001,91234,copybuf(handle),copybuf(pass,sizeof(pass)));impl.Verify(v,&good);
    const auto *token=good.auth_token.Data<hw_auth_token_t>();unsigned char mac[32];
    if(good.error||!token||token->challenge!=91234)return 84;
    guest_auth_sign(token,offsetof(hw_auth_token_t,hmac),mac);
    if(CRYPTO_memcmp(mac,token->hmac,32))return 85;
    VerifyResponse bad;VerifyRequest w(777001,0,copybuf(handle),copybuf(wrong,sizeof(wrong)));impl.Verify(w,&bad);
    if(bad.error!=ERROR_INVALID)return 86;
    EnrollResponse rejected;EnrollRequest re(777001,copybuf(handle),copybuf(next,sizeof(next)),copybuf(wrong,sizeof(wrong)));impl.Enroll(re,&rejected);
    if(rejected.error!=ERROR_INVALID)return 87;
    EnrollResponse changed;EnrollRequest ce(777001,copybuf(handle),copybuf(next,sizeof(next)),copybuf(pass,sizeof(pass)));impl.Enroll(ce,&changed);
    if(changed.error||changed.enrolled_password_handle.Data<password_handle_t>()->user_id!=handle.Data<password_handle_t>()->user_id)return 88;
    handle=copybuf(changed.enrolled_password_handle);
    VerifyResponse newgood;VerifyRequest nv(777001,0,copybuf(handle),copybuf(next,sizeof(next)));impl.Verify(nv,&newgood);
    if(newgood.error)return 89;
    for(int i=0;i<5;i++){
        VerifyResponse fail;VerifyRequest fv(777001,0,copybuf(handle),copybuf(pass,sizeof(pass)));impl.Verify(fv,&fail);
        if(i<4?fail.error!=ERROR_INVALID:(fail.error!=ERROR_RETRY||!fail.retry_timeout))return 90;
    }
    close(records);records=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    GuestGateKeeper restarted;VerifyResponse throttled;VerifyRequest tv(777001,0,copybuf(handle),copybuf(next,sizeof(next)));restarted.Verify(tv,&throttled);
    if(throttled.error!=ERROR_RETRY||!throttled.retry_timeout)return 91;
    puts("PASS: enroll, correct/wrong credential, signed challenge, old-password check, reenroll, preserved SID, persisted retry limit");return 0;
}
int main(int argc,char **argv){
    if(guest_close_inherited_fds())return 2;
    if(argc==3&&!strcmp(argv[1],"selftest"))return selftest(argv[2]);
    if(argc!=1)return 2;
    struct stat a,b,d,h;
    if(stat(BASE"/rootfs/dev/binderfs/binder",&a)||stat("/dev/binder",&b)||a.st_rdev==b.st_rdev||
       stat(BASE"/rootfs/data",&d)||stat("/data",&h)||d.st_dev==h.st_dev)return 3;
    int ledger=open(BASE"/keymint-owned",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(ledger<0||!guest_auth_read(ledger))return 4;close(ledger);
    const char *path=BASE"/rootfs/data/misc/gatekeeper/soft";
    if(mkdir(path,0700)&&errno!=EEXIST)return 5;
    records=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(records<0)return 5;
    if(chdir(BASE"/rootfs/dev/binderfs"))return 6;
    auto sm=gbinder_servicemanager_new2("binder","aidl6","aidl3");
    auto o=gbinder_servicemanager_new_local_object(sm,IFACE,dispatch,nullptr);gbinder_local_object_set_stability(o,GBINDER_STABILITY_VINTF);
    int status=gbinder_servicemanager_add_service_sync(sm,IFACE"/default",o);
    fprintf(stderr,"isolated guest Gatekeeper registration=%d\n",status);if(status)return 7;
    g_main_loop_run(g_main_loop_new(nullptr,false));return 0;
}
