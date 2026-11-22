//! Strict no_std URL/redirect preflight for MayOS-owned GitHub Releases.
//! This cannot establish X.509 trust or download/install anything.
const LIMIT:usize=2048;
fn nibble(c:u8)->Option<u8>{
    match c {b'0'..=b'9'=>Some(c-b'0'),b'a'..=b'f'=>Some(c-b'a'+10),
             b'A'..=b'F'=>Some(c-b'A'+10),_=>None}
}
fn url_bytes_ok(input:&[u8],is_path:bool)->bool{
    let mut i=0;
    while i<input.len(){
        let c=input[i];
        if c<=32||c>=127||c==b'\\'||c==b'#'||c==b'"'||c==b'<'||c==b'>'{
            return false;
        }
        if c==b'%'{
            if i+2>=input.len(){return false}
            let (Some(a),Some(b))=(nibble(input[i+1]),nibble(input[i+2])) else{return false};
            let d=(a<<4)|b;
            if d<=32||d>=127||d==b'\\'||d==b'#'||
                (is_path&&(d==b'.'||d==b'/'||d==b'%'||d==b'?')) {
                return false;
            }
            i+=3;
        }else{i+=1;}
    }
    true
}
fn scoped(path:&[u8],prefix:&[u8])->bool{
    path.starts_with(prefix)&&
        (path.len()==prefix.len()||path.get(prefix.len())==Some(&b'/'))
}
pub(crate) fn release_url_allowed(url:&[u8],redirects:u32)->bool{
    if url.len()<20||url.len()>LIMIT||redirects>3{return false}
    let Some(rest)=url.strip_prefix(b"https://") else{return false};
    let Some(i)=rest.iter().position(|b|*b==b'/') else{return false};
    let authority=&rest[..i];
    if authority.is_empty()||authority.len()>80||
       authority.iter().any(|b|*b==b'@'||*b==b'%'||*b==b'\\'||
                                    *b==b'?'||*b==b'#'){return false}
    let host=authority.strip_suffix(b":443").unwrap_or(authority);
    if host.contains(&b':'){return false}
    let route=&rest[i..];
    let boundary=route.iter().position(|b|*b==b'?').unwrap_or(route.len());
    let path=&route[..boundary];
    if path.len()<2||!url_bytes_ok(path,true)||!url_bytes_ok(&route[boundary..],false){
        return false;
    }
    for segment in path[1..].split(|b|*b==b'/'){
        if segment.is_empty()||segment==b"."||segment==b".."{return false}
    }
    if host==b"api.github.com"{
        scoped(path,b"/repos/ZhongYeMay/MayOS/releases")
    } else if host==b"github.com"{
        scoped(path,b"/ZhongYeMay/MayOS/releases")
    } else if host==b"release-assets.githubusercontent.com"||
              host==b"objects.githubusercontent.com"{
        // Asset URLs are only valid AFTER a checked HTTPS redirect from a
        // trusted releases endpoint, and still need signed-image integrity.
        redirects>0
    }else{false}
}

/// C ABI: 1 means eligible for a future authenticated TLS connection, NEVER
/// proof of certificate identity, network availability or permission to install.
/// SAFETY: the passed pointer must remain valid for length readable bytes.
#[no_mangle]
pub unsafe extern "C" fn mayrs_release_url_allowed(
    data:*const u8,length:u32,redirects:u32
)->i32{
    if data.is_null()||length==0||length as usize>LIMIT{return 0}
    let data=unsafe{core::slice::from_raw_parts(data,length as usize)};
    i32::from(release_url_allowed(data,redirects))
}
/// Pure in-kernel diagnostic; sends no network packets and writes no disk.
#[no_mangle]
pub extern "C" fn mayrs_release_url_selftest()->i32{
    let yes=[
        (b"https://api.github.com/repos/ZhongYeMay/MayOS/releases/latest".as_slice(),0),
        (b"https://github.com/ZhongYeMay/MayOS/releases/download/v1/MayOS.iso".as_slice(),0),
        (b"https://release-assets.githubusercontent.com/file?sv=2026%2F11&sig=a".as_slice(),1),
        (b"https://objects.githubusercontent.com/object/abc?x=y".as_slice(),2),
    ];
    let no=[
        b"http://api.github.com/repos/ZhongYeMay/MayOS/releases/latest".as_slice(),
        b"https://api.github.com.evil.test/repos/ZhongYeMay/MayOS/releases".as_slice(),
        b"https://github.com@evil.test/ZhongYeMay/MayOS/releases".as_slice(),
        b"https://github.com/Other/Repo/releases".as_slice(),
        b"https://github.com/ZhongYeMay/MayOS/releases/../secret".as_slice(),
        b"https://api.github.com:444/repos/ZhongYeMay/MayOS/releases".as_slice(),
        b"https://api.github.com/repos/ZhongYeMay/MayOS/releases%2fother".as_slice(),
        b"https://github.com/ZhongYeMay/MayOS/releases#fragment".as_slice(),
        b"https://release-assets.githubusercontent.com/obj".as_slice(),
    ];
    if yes.iter().any(|(url,hops)|!release_url_allowed(url,*hops))||
       no.iter().any(|url|release_url_allowed(url,0))||
       release_url_allowed(yes[0].0,4){return 0}
    1
}
#[cfg(test)]
mod tests{
    use super::*;
    #[test]
    fn exact_hosts_redirects_and_scoped_repository(){
        assert_eq!(mayrs_release_url_selftest(),1);
        assert!(release_url_allowed(
            b"https://github.com:443/ZhongYeMay/MayOS/releases/download/v1/image.iso",0));
        assert!(!release_url_allowed(b"https://release-assets.githubusercontent.com/file",0));
        assert!(release_url_allowed(b"https://release-assets.githubusercontent.com/file",1));
    }
    #[test]
    fn reject_controls_lookalikes_and_path_traversal(){
        for url in [
            b"http://github.com/ZhongYeMay/MayOS/releases/latest".as_slice(),
            b"https://github.com.evil.test/ZhongYeMay/MayOS/releases/download/x".as_slice(),
            b"https://github.com@evil.test/ZhongYeMay/MayOS/releases".as_slice(),
            b"https://127.0.0.1/ZhongYeMay/MayOS/releases".as_slice(),
            b"https://github.com:80/ZhongYeMay/MayOS/releases".as_slice(),
            b"https://api.github.com/repos/ZhongYeMay/MayOS/releases\r\nHost: evil".as_slice(),
            b"https://api.github.com/repos/ZhongYeMay/MayOS/releases#x".as_slice(),
            b"https://api.github.com/repos/ZhongYeMay/MayOS/releases%0D%0A".as_slice(),
            b"https://github.com/ZhongYeMay/MayOS/releases/%2e%2e/secret".as_slice(),
            b"https://github.com/ZhongYeMay/MayOS/releases//x".as_slice(),
            b"https://github.com/ZhongYeMay/MayOS/releases\\evil".as_slice(),
        ]{assert!(!release_url_allowed(url,0));}
    }
    #[test]
    fn ffi_bounds_and_redirect_cap(){
        let url=b"https://api.github.com/repos/ZhongYeMay/MayOS/releases/latest";
        // SAFETY: static literal is readable throughout these calls.
        assert_eq!(unsafe{mayrs_release_url_allowed(url.as_ptr(),url.len() as u32,0)},1);
        assert_eq!(unsafe{mayrs_release_url_allowed(url.as_ptr(),url.len() as u32,4)},0);
        // SAFETY: a null pointer is rejected before dereferencing.
        assert_eq!(unsafe{mayrs_release_url_allowed(core::ptr::null(),100,0)},0);
        assert!(!release_url_allowed(&[b'a';2049],0));
    }
}
