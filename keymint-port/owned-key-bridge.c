#define _GNU_SOURCE
#include <gbinder.h>
#include "gbinder_local_object_p.h"
#include <glib-unix.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include "guest-fd-hygiene.h"
#include "guest-auth.h"

#define BASE "/data/local/tmp/gsi17-graphics-20261105-01"
#define IFACE "android.hardware.security.keymint.IKeyMintDevice"
#define OPIFACE "android.hardware.security.keymint.IKeyMintOperation"
#define SOFTPREFIX "GuestSoftKeyMint1"
static GBinderServiceManager *guest_sm;
static GBinderClient *hardware, *legacy, *software;
static int ledger = -1;
static unsigned active_ops;

static const unsigned char *position(GBinderReader *r) {
    gsize n=0; const unsigned char *p=gbinder_reader_get_data(r,&n);
    return p ? p+n-gbinder_reader_bytes_remaining(r) : NULL;
}
static GBinderLocalReply *error_reply(GBinderLocalObject *o,int error) {
    GBinderLocalReply *q=gbinder_local_object_new_reply(o); GBinderWriter w;
    gbinder_local_reply_init_writer(q,&w);
    gbinder_writer_append_int32(&w,-8); /* EX_SERVICE_SPECIFIC */
    gbinder_writer_append_string16(&w,"guest KeyMint isolation policy");
    gbinder_writer_append_int32(&w,0); /* no remote stack trace */
    gbinder_writer_append_int32(&w,error); return q;
}
static GBinderRemoteReply *transact(GBinderClient *c,unsigned code,
        const void *body,size_t len,int *status) {
    GBinderLocalRequest *q=gbinder_client_new_request(c); GBinderWriter w;
    gbinder_local_request_init_writer(q,&w);gbinder_writer_append_bytes(&w,body,len);
    GBinderRemoteReply *r=gbinder_client_transact_sync_reply(c,code,q,status);
    gbinder_local_request_unref(q);return r;
}
static int reply_ok(GBinderRemoteReply *reply,GBinderReader *r) {
    gint32 ex=0;gbinder_remote_reply_init_reader(reply,r);
    return gbinder_reader_read_int32(r,&ex)&&!ex;
}
static char *blob_name(const void *blob,size_t n) {
    return g_compute_checksum_for_data(G_CHECKSUM_SHA256,blob,n);
}
static int owned(const void *blob,size_t n) {
    if(!blob||!n||n>65536)return 0;
    char *name=blob_name(blob,n);int fd=openat(ledger,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);g_free(name);
    if(fd<0)return 0;
    struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==1017&&st.st_size==(off_t)n;
    unsigned char *buf=ok?g_malloc(n):NULL;
    if(ok){size_t done=0;while(done<n){ssize_t k=read(fd,buf+done,n-done);if(k<=0){ok=0;break;}done+=k;}if(ok)ok=!memcmp(buf,blob,n);}
    g_free(buf);close(fd);return ok;
}
/* Durable allowlist. A blob is never returned to the guest before fsync. */
static int remember(const void *blob,size_t n) {
    if(!blob||!n||n>65536)return 0;
    if(owned(blob,n))return 1;
    char *name=blob_name(blob,n);char tmp[100];snprintf(tmp,sizeof(tmp),".pending-%ld",(long)getpid());
    int fd=openat(ledger,tmp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0){g_free(name);return 0;}
    size_t done=0;int ok=1;
    while(done<n){ssize_t k=write(fd,(const char*)blob+done,n-done);if(k<=0){ok=0;break;}done+=k;}
    if(fsync(fd))ok=0;close(fd);
    if(ok&&renameat(ledger,tmp,ledger,name))ok=0;
    if(ok&&fsync(ledger))ok=0;
    if(!ok)unlinkat(ledger,tmp,0);
    g_free(name);return ok;
}
static int params(GBinderReader *r,int *no_auth) {
    gint32 count=0;if(!gbinder_reader_read_int32(r,&count)||count<0||count>256)return 0;
    if(no_auth)*no_auth=0;
    for(int i=0;i<count;i++){
        GBinderReader p;gboolean present=0;gint32 tag=0,exists=0,type=0,value=0;
        if(!gbinder_reader_start_parcelable(r,&p,&present)||!present)return 0;
        if(!gbinder_reader_read_int32(&p,&tag))return 0;
        if(no_auth&&tag==0x700001f7&&gbinder_reader_read_int32(&p,&exists)&&exists&&
            gbinder_reader_read_int32(&p,&type)&&type==10&&gbinder_reader_read_int32(&p,&value)&&value)*no_auth=1;
        gbinder_reader_finish_parcelable(&p);
    }return 1;
}
static GBinderClient *route(const void **blob,size_t *n) {
    const unsigned char *p=*blob;size_t len=*n;
    if(owned(p,len))return hardware;
    if(len>8&&!memcmp(p,"pKMblob\1",8))return legacy;
    if(len>sizeof(SOFTPREFIX)-1&&!memcmp(p,SOFTPREFIX,sizeof(SOFTPREFIX)-1)){
        *blob=p+sizeof(SOFTPREFIX)-1;*n=len-(sizeof(SOFTPREFIX)-1);return software;
    }
    static const char prefix[]="SoftKeyMintForV1Blob",key[]="SoftKeyMintForV1HMACKey";
    if(len>sizeof(prefix)-1+32&&!memcmp(p,prefix,sizeof(prefix)-1)){
        const unsigned char *inner=p+sizeof(prefix)-1;size_t inner_n=len-(sizeof(prefix)-1)-32;
        GHmac *h=g_hmac_new(G_CHECKSUM_SHA256,(const guchar*)key,sizeof(key)-1);
        g_hmac_update(h,inner,inner_n);unsigned char digest[32];gsize dn=32;g_hmac_get_digest(h,digest,&dn);g_hmac_unref(h);
        if(!memcmp(digest,inner+inner_n,32)){*blob=inner;*n=inner_n;return software;}
    }
    /* Earlier guest releases used CreateKeyMintDevice(SOFTWARE) directly.
     * Those integrity-assured version-0 blobs have no compat wrapper. Only
     * the isolated software implementation may validate them; they never
     * reach the hardware endpoint. Do not rewrite the stored key database. */
    if(len>=32&&p[0]==0)return software;
    return NULL;
}
/* Software KeyMint's enforcement mock does not check token age. Read the
 * authenticated blob's actual timeout rather than trusting begin parameters. */
static int auth_timeout(GBinderClient *target,const void *blob,size_t n,GBinderReader args,uint64_t *age) {
    gint32 count;const void *appid=NULL,*appdata=NULL;gsize idn=0,datan=0;
    if(!gbinder_reader_read_int32(&args,&count)||count<0||count>256)return 0;
    for(int i=0;i<count;i++){
        GBinderReader p;gboolean present;gint32 tag,exists,type;
        if(!gbinder_reader_start_parcelable(&args,&p,&present)||!present||!gbinder_reader_read_int32(&p,&tag))return 0;
        if(tag==(gint32)0x90000259||tag==(gint32)0x900002bc){
            if(!gbinder_reader_read_int32(&p,&exists)||!exists||!gbinder_reader_read_int32(&p,&type)||type!=14)return 0;
            if(tag==(gint32)0x90000259)appid=gbinder_reader_read_byte_array(&p,&idn);
            else appdata=gbinder_reader_read_byte_array(&p,&datan);
        }
        gbinder_reader_finish_parcelable(&p);
    }
    GBinderLocalRequest *q=gbinder_client_new_request(target);GBinderWriter w;gbinder_local_request_init_writer(q,&w);
    gbinder_writer_append_byte_array(&w,blob,n);
    /* These AIDL arrays are non-nullable. libgbinder serializes a zero-length
     * byte array as -1 (null), so explicitly encode an empty vector as 0. */
    if(idn)gbinder_writer_append_byte_array(&w,appid,idn);else gbinder_writer_append_int32(&w,0);
    if(datan)gbinder_writer_append_byte_array(&w,appdata,datan);else gbinder_writer_append_int32(&w,0);
    int status=0;GBinderRemoteReply *reply=gbinder_client_transact_sync_reply(target,14,q,&status);gbinder_local_request_unref(q);
    if(!reply)return 0;
    GBinderReader r;int ok=!status&&reply_ok(reply,&r)&&gbinder_reader_read_int32(&r,&count)&&count>=0&&count<16;
    *age=15000; /* Fresh token for per-operation authentication. */
    for(int i=0;ok&&i<count;i++){
        GBinderReader ch;gboolean present;gint32 level,pc;
        ok=gbinder_reader_start_parcelable(&r,&ch,&present)&&present&&gbinder_reader_read_int32(&ch,&level)&&gbinder_reader_read_int32(&ch,&pc)&&pc>=0&&pc<=256;
        for(int j=0;ok&&j<pc;j++){
            GBinderReader p;gint32 tag,exists,type,timeout;
            ok=gbinder_reader_start_parcelable(&ch,&p,&present)&&present&&gbinder_reader_read_int32(&p,&tag);
            if(ok&&tag==0x300001f9){
                ok=gbinder_reader_read_int32(&p,&exists)&&exists&&gbinder_reader_read_int32(&p,&type)&&type==11&&gbinder_reader_read_int32(&p,&timeout);
                if(ok)*age=(uint64_t)(uint32_t)timeout*1000;
            }
            if(ok)gbinder_reader_finish_parcelable(&p);
        }
        if(ok)gbinder_reader_finish_parcelable(&ch);
    }
    gbinder_remote_reply_unref(reply);return ok;
}
typedef struct Operation {
    GBinderClient *client;
    GBinderLocalObject *local;
    gulong acquired;
    guint unclaimed;
    int closed;
    int guest_software;
    uint64_t max_age_ms;
} Operation;
static void operation_free(void *data) {
    Operation *op=data;
    if(!op->closed){
        int status=0;
        GBinderRemoteReply *r=transact(op->client,4,NULL,0,&status);
        if(r)gbinder_remote_reply_unref(r);
        active_ops--;
    }
    if(op->client)gbinder_client_unref(op->client);
    g_free(op);
}
static gboolean operation_unclaimed(void *data) {
    Operation *op=data;op->unclaimed=0;
    gbinder_local_object_remove_handler(op->local,op->acquired);
    /* A client that never acquired the returned endpoint cannot pin a TEE op. */
    gbinder_local_object_drop(op->local);
    return G_SOURCE_REMOVE;
}
static void operation_acquired(GBinderLocalObject *local,void *data) {
    Operation *op=data;
    if(local->strong_refs>0){
        if(op->unclaimed){g_source_remove(op->unclaimed);op->unclaimed=0;}
        gbinder_local_object_remove_handler(local,op->acquired);
        /* Binder now retains the endpoint. Last release runs operation_free. */
        gbinder_local_object_unref(local);
    }
}
static GBinderLocalReply *operation_call(GBinderLocalObject *o,GBinderRemoteRequest *req,
    guint code,guint flags,int *status,void *data) {
    Operation *op=data;*status=0;
    if(gbinder_remote_request_sender_euid(req)!=1017||flags||
       strcmp(gbinder_remote_request_interface(req),OPIFACE))return error_reply(o,-38);
    if(code<1||code>4||op->closed)return error_reply(o,-28);
    GBinderReader r;gbinder_remote_request_init_reader(req,&r);
    size_t n=gbinder_reader_bytes_remaining(&r);if(n>1048576)return error_reply(o,-38);
    if(op->guest_software&&code<4){
        GBinderReader check=r;gsize bytes=0;
        gbinder_reader_read_byte_array(&check,&bytes);
        if(code==3)gbinder_reader_read_byte_array(&check,&bytes);
        if(!guest_auth_validate(&check,op->max_age_ms))return error_reply(o,-26);
    }
    GBinderRemoteReply *reply=transact(op->client,code,position(&r),n,status);
    if(code==3||code==4){op->closed=1;active_ops--;gbinder_client_unref(op->client);op->client=NULL;}
    if(!reply)return NULL;
    GBinderLocalReply *out=gbinder_remote_reply_copy_to_local(reply);gbinder_remote_reply_unref(reply);return out;
}
static GBinderLocalReply *begin_reply(GBinderLocalObject *o,GBinderRemoteReply *reply,int is_soft,uint64_t max_age_ms) {
    GBinderReader r,p;gboolean present=0;gint64 challenge=0;
    if(!reply_ok(reply,&r)||!gbinder_reader_start_parcelable(&r,&p,&present)||!present||
       !gbinder_reader_read_int64(&p,&challenge))return error_reply(o,-1000);
    /* BeginResult is the only payload. Its parameter bytes contain no handles. */
    const unsigned char *first=position(&p);if(!params(&p,NULL))return error_reply(o,-1000);
    const unsigned char *last=position(&p);
    GBinderRemoteObject *remote=gbinder_reader_read_object(&p);if(!remote)return error_reply(o,-1000);
    Operation *op=g_new0(Operation,1);op->guest_software=is_soft;op->max_age_ms=max_age_ms;op->client=gbinder_client_new(remote,OPIFACE);gbinder_remote_object_unref(remote);
    GBinderLocalObject *local=gbinder_servicemanager_new_local_object(guest_sm,OPIFACE,operation_call,op);
    gbinder_local_object_set_stability(local,GBINDER_STABILITY_VINTF);active_ops++;
    op->local=local;
    g_object_set_data_full(G_OBJECT(local),"guest-operation",op,operation_free);
    op->acquired=gbinder_local_object_add_strong_refs_changed_handler(local,operation_acquired,op);
    op->unclaimed=g_timeout_add_seconds(30,operation_unclaimed,op);
    GBinderLocalReply *out=gbinder_local_object_new_reply(o);GBinderWriter w,result;
    gbinder_local_reply_init_writer(out,&w);gbinder_writer_append_int32(&w,0);
    gbinder_writer_start_parcelable(&w,&result);gbinder_writer_append_int64(&result,challenge);
    gbinder_writer_append_bytes(&result,first,last-first);gbinder_writer_append_local_object(&result,local);
    gbinder_writer_finish_parcelable(&result);
    /* Initial reference lasts until Binder acquires it, or the claim expires. */
    return out;
}
static GBinderLocalReply *device_call(GBinderLocalObject *o,GBinderRemoteRequest *req,
    guint code,guint flags,int *status,void *unused) {
    (void)unused;*status=0;
    uid_t uid=gbinder_remote_request_sender_euid(req);
    if(uid!=1017||flags||strcmp(gbinder_remote_request_interface(req),IFACE))return error_reply(o,-38);
    fprintf(stderr,"device method=%u uid=%u\n",code,uid);fflush(stderr);
    /* Global KeyMint lifecycle/state/storage operations NEVER reach the host. */
    if(!((code>=1&&code<=7)||code==10||code==14||code==0xffffff||code==0xfffffe))return error_reply(o,-100);
    if(code==5)return error_reply(o,-100); /* Wrapped import not implemented. */
    if(code==10&&active_ops>=64)return error_reply(o,-31);
    GBinderReader r;gbinder_remote_request_init_reader(req,&r);
    const unsigned char *body=position(&r);size_t len=gbinder_reader_bytes_remaining(&r);
    if(len>131072)return error_reply(o,-38);
    GBinderClient *target=hardware;GBinderLocalRequest *rewritten=NULL;
    const void *blob=NULL;size_t blob_n=0;int no_auth=0;uint64_t max_age_ms=15000;
    if(code==3||code==4){
        if(!params(&r,&no_auth))return error_reply(o,-38);
        if(code==4){gint32 format;if(!gbinder_reader_read_int32(&r,&format))return error_reply(o,-38);
            gsize n=0;gbinder_reader_read_byte_array(&r,&n);if(n>65536)return error_reply(o,-38);}
        GBinderReader a;gboolean present=0;
        if(!gbinder_reader_start_parcelable(&r,&a,&present))return error_reply(o,-38);
        if(present){gsize n=0;const void *att=gbinder_reader_read_byte_array(&a,&n);
            if(!owned(att,n))return error_reply(o,-33);gbinder_reader_finish_parcelable(&a);}
        if(!gbinder_reader_at_end(&r))return error_reply(o,-38);
        if(!no_auth){if(present)return error_reply(o,-100);target=software;}
    } else if(code==6||code==7||code==10||code==14){
        gint32 purpose=0;if(code==10&&!gbinder_reader_read_int32(&r,&purpose))return error_reply(o,-38);
        blob=gbinder_reader_read_byte_array(&r,&blob_n);if(!blob||!blob_n)return error_reply(o,-33);
        const void *inner=blob;size_t inner_n=blob_n;target=route(&inner,&inner_n);
        fprintf(stderr,"blob route=%s bytes=%zu\n",target==hardware?"owned-tee":target==software?"guest-software":target==legacy?"legacy-software":"rejected",blob_n);
        if(!target)return error_reply(o,-33);
        if(code==10&&target!=hardware){
            GBinderReader check=r;
            if(!auth_timeout(target,inner,inner_n,r,&max_age_ms)||!params(&check,NULL)||
               !guest_auth_validate(&check,max_age_ms))return error_reply(o,-26);
        }
        if(code==7&&!gbinder_reader_at_end(&r))return error_reply(o,-38);
        if(inner!=blob){
            rewritten=gbinder_client_new_request(target);GBinderWriter w;gbinder_local_request_init_writer(rewritten,&w);
            if(code==10)gbinder_writer_append_int32(&w,purpose);
            gbinder_writer_append_byte_array(&w,inner,inner_n);
            gbinder_writer_append_bytes(&w,position(&r),gbinder_reader_bytes_remaining(&r));
        }
    }
    GBinderRemoteReply *reply;
    if(rewritten){reply=gbinder_client_transact_sync_reply(target,code,rewritten,status);gbinder_local_request_unref(rewritten);}
    else reply=transact(target,code,body,len,status);
    if(!reply)return NULL;
    GBinderLocalReply *out=NULL;GBinderReader rr,p;gboolean present=0;
    if(!reply_ok(reply,&rr))out=gbinder_remote_reply_copy_to_local(reply);
    else if(code==3||code==4||code==6){
        if(code!=6&&(!gbinder_reader_start_parcelable(&rr,&p,&present)||!present))out=error_reply(o,-1000);
        else {
            GBinderReader *br=code==6?&rr:&p;gsize n=0;const void *created=gbinder_reader_read_byte_array(br,&n);
            if(!created||!n)out=error_reply(o,-1000);
            else if(target==hardware){
                if(remember(created,n))out=gbinder_remote_reply_copy_to_local(reply);
                else {
                    fprintf(stderr,"ERROR: unable to journal generated key\n");
                    if(code==3||code==4){
                        GBinderLocalRequest *cleanup=gbinder_client_new_request(hardware);GBinderWriter cw;
                        gbinder_local_request_init_writer(cleanup,&cw);gbinder_writer_append_byte_array(&cw,created,n);
                        int cs=0;GBinderRemoteReply *cr=gbinder_client_transact_sync_reply(hardware,7,cleanup,&cs);
                        gbinder_local_request_unref(cleanup);if(cr)gbinder_remote_reply_unref(cr);
                    }
                    out=error_reply(o,-1000);
                }
            }else if(target==software){
                /* Software provenance remains explicit and survives restarts. */
                GByteArray *wrapped=g_byte_array_new();g_byte_array_append(wrapped,(const guint8*)SOFTPREFIX,sizeof(SOFTPREFIX)-1);g_byte_array_append(wrapped,created,n);
                out=gbinder_local_object_new_reply(o);GBinderWriter w,result;gbinder_local_reply_init_writer(out,&w);gbinder_writer_append_int32(&w,0);
                if(code!=6)gbinder_writer_start_parcelable(&w,&result);
                GBinderWriter *dst=code==6?&w:&result;gbinder_writer_append_byte_array(dst,wrapped->data,wrapped->len);
                if(code!=6){gbinder_writer_append_bytes(dst,position(br),gbinder_reader_bytes_remaining(br));gbinder_writer_finish_parcelable(&result);}
                g_byte_array_unref(wrapped);
            }else out=gbinder_remote_reply_copy_to_local(reply);
        }
    }else if(code==10)out=begin_reply(o,reply,target!=hardware,max_age_ms);
    else out=gbinder_remote_reply_copy_to_local(reply);
    gbinder_remote_reply_unref(reply);return out;
}
static GBinderClient *compat_device(GBinderClient *compat,int level) {
    GBinderRemoteReply *r=transact(compat,1,&level,4,&(int){0});if(!r)return NULL;
    GBinderReader reader;GBinderClient *c=NULL;
    if(reply_ok(r,&reader)){GBinderRemoteObject *o=gbinder_reader_read_object(&reader);
        if(o){c=gbinder_client_new(o,IFACE);gbinder_remote_object_unref(o);}}
    gbinder_remote_reply_unref(r);return c;
}
static gboolean stop_loop(void *loop){g_main_loop_quit(loop);return G_SOURCE_REMOVE;}
int main(int argc,char **argv){
    const char *dev=BASE"/rootfs/dev/binderfs/binder";
    if(argc!=2||(strcmp(argv[1],"probe")&&strcmp(argv[1],"default"))||guest_close_inherited_fds())return 2;
    struct stat a,b;if(stat(dev,&a)||stat("/dev/binder",&b)||a.st_rdev==b.st_rdev)return 3;
    if(mkdir(BASE"/keymint-owned",0700)&&errno!=EEXIST)return 4;
    ledger=open(BASE"/keymint-owned",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    const char *label="u:object_r:keystore_data_file:s0";
    if(ledger<0||fchown(ledger,1017,1017)||fchmod(ledger,0700)||fsetxattr(ledger,"security.selinux",label,strlen(label)+1,0))return 4;
    if(!guest_auth_create(ledger))return 4;
    /* Avoid traversing host shell_data_file directories after dropping identity. */
    if(chdir(BASE"/rootfs/dev/binderfs"))return 4;
    /* Change identity before libgbinder starts worker threads. */
    if(setgroups(0,NULL)||setgid(1017)||setuid(1017))return 6;
    const char *ctx="u:r:keystore:s0";int fd=open("/proc/self/attr/current",O_WRONLY|O_CLOEXEC);
    if(fd<0||write(fd,ctx,strlen(ctx))!=(ssize_t)strlen(ctx))return 6;close(fd);
    guest_sm=gbinder_servicemanager_new2("binder","aidl6","aidl3");if(!guest_sm)return 5;
    GBinderServiceManager *host=gbinder_servicemanager_new2("/dev/binder","aidl6","aidl3");int status=0;
    GBinderRemoteObject *h=gbinder_servicemanager_get_service_sync(host,IFACE"/default",&status);
    if(!h){fprintf(stderr,"hardware lookup=%d\n",status);return 7;}hardware=gbinder_client_new(h,IFACE);
    GBinderRemoteObject *c=gbinder_servicemanager_get_service_sync(guest_sm,!strcmp(argv[1],"probe")?"android.security.compat":"android.security.guestcompat",&status);
    if(!c){fprintf(stderr,"compat lookup=%d\n",status);return 8;}
    GBinderClient *compat=gbinder_client_new(c,"android.security.compat.IKeystoreCompatService");
    legacy=compat_device(compat,1);software=compat_device(compat,0);gbinder_client_unref(compat);
    if(!legacy||!software){fprintf(stderr,"compat devices unavailable\n");return 9;}
    GBinderLocalObject *local=gbinder_servicemanager_new_local_object(guest_sm,IFACE,device_call,NULL);
    gbinder_local_object_set_stability(local,!strcmp(argv[1],"default")?GBINDER_STABILITY_VINTF:GBINDER_STABILITY_SYSTEM);
    const char *name=!strcmp(argv[1],"default")?IFACE"/default":"android.security.keystore";
    status=gbinder_servicemanager_add_service_sync(guest_sm,name,local);
    fprintf(stderr,"guarded hardware + legacy software registration=%d name=%s\n",status,name);fflush(stderr);
    if(status)return 10;
    GMainLoop *loop=g_main_loop_new(NULL,0);g_unix_signal_add(SIGTERM,stop_loop,loop);g_main_loop_run(loop);return 0;
}
