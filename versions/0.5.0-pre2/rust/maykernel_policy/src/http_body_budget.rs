//! HTTP Release body byte-budget state machine (no_std, no heap).
//! This is NOT an HTTPS client, disk writer, hash/signature validator or
//! installer. It protects future already-authenticated TLS streams against
//! truncated/extra payload, invalid chunk size and length mismatch.
use crate::http_framing::MayHttpHeaders;
const MAX_BODY:u32=512*1024;
const MAX_CHUNK:u32=4096;
const ACTIVE:u32=1;
const COMPLETE:u32=2;
const CLOSED:u32=3;
const FAILED:u32=4;

#[repr(C)]
#[derive(Clone,Copy)]
pub struct MayHttpBodyBudget {
    pub declared:u32,
    pub received:u32,
    pub phase:u32,
    pub reserved:u32,
}
impl MayHttpBodyBudget {
    const fn blank()->Self {
        Self{declared:0,received:0,phase:0,reserved:0}
    }
}
fn begin(headers:&MayHttpHeaders,signed_len:u32)->Option<MayHttpBodyBudget> {
    if headers.status!=200 || headers.header_bytes<16 ||
       headers.header_bytes>2048 || headers.redirect_length!=0 ||
       signed_len==0 || signed_len>MAX_BODY ||
       headers.content_length!=signed_len {return None}
    Some(MayHttpBodyBudget{
        declared:signed_len,received:0,phase:ACTIVE,reserved:0,
    })
}
fn consume(ctx:&mut MayHttpBodyBudget,length:u32)->i32 {
    if ctx.phase!=ACTIVE || ctx.reserved!=0 ||
       ctx.declared==0 || ctx.declared>MAX_BODY ||
       ctx.received>=ctx.declared ||
       length==0 || length>MAX_CHUNK ||
       length>ctx.declared-ctx.received {
        ctx.phase=FAILED;
        return -1;
    }
    ctx.received+=length;
    if ctx.received==ctx.declared {
        ctx.phase=COMPLETE;
        2
    }else{1}
}
fn finish(ctx:&mut MayHttpBodyBudget)->i32{
    if ctx.phase==COMPLETE && ctx.received==ctx.declared &&
       ctx.declared!=0 && ctx.declared<=MAX_BODY && ctx.reserved==0 {
        ctx.phase=CLOSED;
        1
    }else{
        ctx.phase=FAILED;
        -1
    }
}
/// Begin strictly bounded body acceptance ONLY AFTER caller verifies TLS
/// origin and independently authenticates an Ed25519-signed image length.
/// This function DOES NOT verify either of those properties itself.
/// SAFETY: headers points to a readable valid C layout and out to exclusive
/// writable MayHttpBodyBudget storage.
#[no_mangle]
pub unsafe extern "C" fn mayrs_http_body_begin(
    headers:*const MayHttpHeaders, signed_len:u32,out:*mut MayHttpBodyBudget
)->i32{
    if headers.is_null()||out.is_null(){return 0}
    // SAFETY: caller guarantees the pointer validity and exclusive output.
    let headers=unsafe{&*headers};
    let Some(value)=begin(headers,signed_len)else{return 0};
    unsafe{core::ptr::write(out,value)};
    1
}
/// Accept a LENGTH of an authenticated TLS plaintext chunk <=4096 bytes.
/// No buffer ownership, cryptographic check or disk write is done here.
/// Return 1 continuing, 2 exact expected length reached, -1 rejected.
/// SAFETY: ctx must point to an exclusively held valid writable context.
#[no_mangle]
pub unsafe extern "C" fn mayrs_http_body_consume(
    ctx:*mut MayHttpBodyBudget,length:u32
)->i32{
    if ctx.is_null(){return -1}
    consume(unsafe{&mut *ctx},length)
}
/// Called only at authenticated TLS end-of-stream; truncated response fails.
/// SAFETY: ctx is exclusively writable valid context.
#[no_mangle]
pub unsafe extern "C" fn mayrs_http_body_finish(
    ctx:*mut MayHttpBodyBudget
)->i32{
    if ctx.is_null(){return -1}
    finish(unsafe{&mut *ctx})
}
/// Native QEMU read-only diagnostic.
#[no_mangle]
pub extern "C" fn mayrs_http_body_selftest()->i32{
    let headers=MayHttpHeaders{
        status:200,header_bytes:42,content_length:65536,
        redirect_length:0,redirect:[0;1024],
    };
    let Some(mut ctx)=begin(&headers,65536)else{return 0};
    for _ in 0..15{
        if consume(&mut ctx,4096)!=1{return 0}
    }
    if consume(&mut ctx,4096)!=2||finish(&mut ctx)!=1{return 0}
    if consume(&mut ctx,1)!=-1{return 0}
    let Some(mut short)=begin(&headers,65536)else{return 0};
    if consume(&mut short,4096)!=1||finish(&mut short)!=-1{return 0}
    let mut wrong=headers;
    wrong.content_length=65535;
    if begin(&wrong,65536).is_some(){return 0}
    1
}
#[cfg(test)]
mod tests{
    use super::*;
    #[test]
    fn complete_64k_split_stream_and_reject_extra(){
        assert_eq!(mayrs_http_body_selftest(),1);
        let h=MayHttpHeaders{status:200,header_bytes:50,content_length:65536,
            redirect_length:0,redirect:[0;1024]};
        let mut state=begin(&h,65536).unwrap();
        for _ in 0..16*8-1 { assert_eq!(consume(&mut state,512),1); }
        assert_eq!(consume(&mut state,512),2);
        assert_eq!(finish(&mut state),1);
        assert_eq!(consume(&mut state,1),-1);
    }
    #[test]
    fn reject_signed_length_mismatch_and_redirect(){
        let mut h=MayHttpHeaders{status:200,header_bytes:43,content_length:5,
            redirect_length:0,redirect:[0;1024]};
        assert!(begin(&h,6).is_none());
        h.status=302;
        assert!(begin(&h,5).is_none());
        h.status=200;h.redirect_length=1;
        assert!(begin(&h,5).is_none());
        h.redirect_length=0;h.content_length=MAX_BODY+1;
        assert!(begin(&h,MAX_BODY+1).is_none());
    }
    #[test]
    fn permanent_failure_on_truncation_overshoot_and_bad_chunks(){
        let h=MayHttpHeaders{status:200,header_bytes:50,content_length:8192,
            redirect_length:0,redirect:[0;1024]};
        let mut s=begin(&h,8192).unwrap();
        assert_eq!(consume(&mut s,4096),1);
        assert_eq!(finish(&mut s),-1);
        assert_eq!(consume(&mut s,4096),-1);
        s=begin(&h,8192).unwrap();
        assert_eq!(consume(&mut s,4097),-1);
        s=begin(&h,8192).unwrap();
        assert_eq!(consume(&mut s,0),-1);
        s=begin(&h,8192).unwrap();
        assert_eq!(consume(&mut s,4096),1);
        assert_eq!(consume(&mut s,4096),2);
        assert_eq!(consume(&mut s,1),-1);
    }
    #[test]
    fn pointer_and_state_safety(){
        let h=MayHttpHeaders{status:200,header_bytes:50,content_length:512,
            redirect_length:0,redirect:[0;1024]};
        let mut s=MayHttpBodyBudget::blank();
        // SAFETY: pointer arguments reference valid stack allocations.
        assert_eq!(unsafe{mayrs_http_body_begin(&h,512,&mut s)},1);
        assert_eq!(unsafe{mayrs_http_body_consume(&mut s,512)},2);
        assert_eq!(unsafe{mayrs_http_body_finish(&mut s)},1);
        // SAFETY: null pointers are rejected before dereference.
        assert_eq!(unsafe{mayrs_http_body_begin(core::ptr::null(),512,&mut s)},0);
        assert_eq!(unsafe{mayrs_http_body_consume(core::ptr::null_mut(),100)},-1);
    }
}
