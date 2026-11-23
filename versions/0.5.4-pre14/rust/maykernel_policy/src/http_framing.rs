//! Strict HTTP/1.1 framing for a FUTURE authenticated HTTPS update stream.
//! Pure no_std code: no network I/O, no certificate trust, no install.
use crate::release_url::release_url_allowed;
const MAX_HEADER:usize=2048;
const MAX_BODY:u32=512*1024;
const MAX_LOCATION:usize=1024;
#[repr(C)]
pub struct MayHttpHeaders {
    pub status:u32,
    pub header_bytes:u32,
    pub content_length:u32,
    pub redirect_length:u32,
    pub redirect:[u8;MAX_LOCATION],
}
impl MayHttpHeaders{
    fn zero()->Self{Self{
        status:0,header_bytes:0,content_length:0,
        redirect_length:0,redirect:[0u8;MAX_LOCATION]}}
}
fn eqcase(a:&[u8],b:&[u8])->bool{
    a.len()==b.len()&&a.iter().zip(b.iter())
        .all(|(x,y)|x.to_ascii_lowercase()==*y)
}
fn decimal(s:&[u8])->Option<u32>{
    if s.is_empty()||s.len()>10{return None}
    let mut v=0u32;
    for &c in s{
        if !c.is_ascii_digit(){return None}
        v=v.checked_mul(10)?.checked_add(u32::from(c-b'0'))?;
    }
    Some(v)
}
fn trim(mut s:&[u8])->&[u8]{
    while s.first().is_some_and(|c| *c==b' '||*c==b'\t'){s=&s[1..]}
    while s.last().is_some_and(|c| *c==b' '||*c==b'\t'){s=&s[..s.len()-1]}
    s
}
fn token(s:&[u8])->bool{
    !s.is_empty()&&s.iter().all(|&c|c.is_ascii_alphanumeric()||
        b"!#$%&'*+-.^_|~".contains(&c))
}
fn value_ok(s:&[u8])->bool{
    !s.is_empty()&&s.iter().all(|&c|(32..127).contains(&c))
}
fn headers(data:&[u8],hops:u32)->Result<Option<MayHttpHeaders>,()>{
    if hops>3{return Err(())}
    let bound=data.len().min(MAX_HEADER);
    let end=match data[..bound].windows(4).position(|s|s==b"\r\n\r\n"){
        Some(offset)=>offset+4,
        None if data.len()<MAX_HEADER=>return Ok(None),
        None=>return Err(()),
    };
    let head=&data[..end];
    let start=head.windows(2).position(|s|s==b"\r\n").ok_or(())?;
    let line=&head[..start];
    if !value_ok(line){return Err(())}
    let status=if line.starts_with(b"HTTP/1.1 200 "){200}
        else if line.starts_with(b"HTTP/1.1 302 "){302}
        else if line.starts_with(b"HTTP/1.1 307 "){307}
        else if line.starts_with(b"HTTP/1.1 308 "){308}
        else{return Err(())};
    let mut parsed=MayHttpHeaders::zero();
    parsed.status=status;
    parsed.header_bytes=end as u32;
    let mut offset=start+2;
    let mut got_length=false;
    let mut got_location=false;
    let mut got_encoding=false;
    while offset<end-2{
        let n=head[offset..].windows(2).position(|w|w==b"\r\n").ok_or(())?;
        let line=&head[offset..offset+n];
        offset+=n+2;
        if line.is_empty()||line.contains(&b'\r')||line.contains(&b'\n'){
            return Err(());
        }
        let colon=line.iter().position(|&c|c==b':').ok_or(())?;
        let key=&line[..colon];
        let value=trim(&line[colon+1..]);
        if !token(key)||!value_ok(value){return Err(())}
        if eqcase(key,b"content-length"){
            if got_length{return Err(())}
            got_length=true;
            parsed.content_length=decimal(value).ok_or(())?;
            if parsed.content_length>MAX_BODY{return Err(())}
        }else if eqcase(key,b"location"){
            if got_location||value.len()>MAX_LOCATION{return Err(())}
            got_location=true;
            parsed.redirect_length=value.len() as u32;
            parsed.redirect[..value.len()].copy_from_slice(value);
        }else if eqcase(key,b"transfer-encoding")||
                 eqcase(key,b"upgrade")||
                 eqcase(key,b"proxy-authenticate"){
            return Err(());
        }else if eqcase(key,b"content-encoding"){
            if got_encoding||!eqcase(value,b"identity"){return Err(())}
            got_encoding=true;
        }
    }
    if offset!=end-2{return Err(())}
    if status==200{
        if !got_length||parsed.content_length==0||got_location{
            return Err(());
        }
    }else{
        if !got_location||!got_length||parsed.content_length!=0{
            return Err(());
        }
        if !release_url_allowed(
            &parsed.redirect[..parsed.redirect_length as usize],hops+1){
            return Err(());
        }
    }
    Ok(Some(parsed))
}
/// Returns 1 valid, 0 need more headers, -1 reject.
/// SAFETY: data must be readable for length, out writable and exclusively held.
#[no_mangle]
pub unsafe extern "C" fn mayrs_http_headers(
    data:*const u8,length:u32,hops:u32,out:*mut MayHttpHeaders
)->i32{
    if data.is_null()||out.is_null()||length==0||length as usize>MAX_HEADER{
        return -1
    }
    let input=unsafe{core::slice::from_raw_parts(data,length as usize)};
    match headers(input,hops){
        Ok(Some(result))=>{unsafe{core::ptr::write(out,result)};1}
        Ok(None)=>0,
        Err(())=>-1,
    }
}
/// Real native diagnostic, no network packets, no data accepted for install.
#[no_mangle]
pub extern "C" fn mayrs_http_headers_selftest()->i32{
    let ok=b"HTTP/1.1 200 OK\r\nContent-Length: 65536\r\n\r\n";
    let redirect=b"HTTP/1.1 302 Found\r\nLocation: https://release-assets.githubusercontent.com/asset\r\nContent-Length: 0\r\n\r\n";
    let duplicate=b"HTTP/1.1 200 OK\r\nContent-Length: 9\r\nContent-Length: 4\r\n\r\n";
    let downgraded=b"HTTP/1.1 302 Found\r\nLocation: http://evil.test/file\r\nContent-Length: 0\r\n\r\n";
    if matches!(headers(ok,0),Ok(Some(v)) if v.content_length==65536)
       && matches!(headers(redirect,0),Ok(Some(v)) if v.status==302)
       && headers(duplicate,0).is_err()
       && headers(downgraded,0).is_err(){1}else{0}
}
#[cfg(test)]
mod tests{
    use super::*;
    #[test]
    fn success_and_safe_redirect(){
        assert_eq!(mayrs_http_headers_selftest(),1);
        assert!(matches!(headers(
            b"HTTP/1.1 200 OK\r\nContent-Length: 524288\r\n\r\n",0),
            Ok(Some(v)) if v.content_length==524288));
    }
    #[test]
    fn rejects_ambiguous_and_overlarge(){
        for data in [
            b"HTTP/1.1 200 OK\r\nContent-Length: 10\r\nContent-Length: 11\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 10\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Length: 524289\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Length: 1,2\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 4\r\n\r\n".as_slice(),
            b"HTTP/1.1 302 Found\r\nLocation: http://evil.test/x\r\nContent-Length: 0\r\n\r\n".as_slice(),
            b"HTTP/1.1 302 Found\r\nLocation: https://evil.test/x\r\nContent-Length: 0\r\n\r\n".as_slice(),
            b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n folded\r\n\r\n".as_slice(),
        ]{assert!(headers(data,0).is_err());}
        assert!(matches!(headers(b"HTTP/1.1 200 OK\r\nContent-Len",0),Ok(None)));
    }
    #[test]
    fn checked_ffi(){
        let mut out=MayHttpHeaders::zero();
        let data=b"HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\n";
        // SAFETY: static bytes and valid writable out.
        assert_eq!(unsafe{mayrs_http_headers(data.as_ptr(),data.len() as u32,0,&mut out)},1);
        assert_eq!(out.content_length,12);
        // SAFETY: NULL data is rejected before reads.
        assert_eq!(unsafe{mayrs_http_headers(core::ptr::null(),12,0,&mut out)},-1);
    }
}
