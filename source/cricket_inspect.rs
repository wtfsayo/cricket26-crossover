use std::{collections::HashMap, path::{Path, Component}};
use tokio::fs::File;
use msixvc::xvd::XvdFile;
#[path = "../src/license.rs"]
mod license;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let args: Vec<String> = std::env::args().collect();
    let source = args.get(1).ok_or("package path required")?;
    let mut input = File::open(source).await?;
    let xvd = XvdFile::parse(&mut input).await?;
    let mut files = HashMap::new();
    for (name, segment) in xvd.parse_user_package_files(&mut input).await? {
        if name == "SegmentMetadata.bin" {
            files.extend(xvd.parse_segment_metadata(&mut input, &segment).await?);
        }
    }
    files.extend(xvd.parse_ntfs_segment_metadata(&mut input, !files.is_empty()).await?);
    if args.len() == 2 {
        let rows: Vec<_> = files.iter().map(|(name, info)| serde_json::json!({
            "path": name.replace('\\', "/"), "bytes": info.length,
            "keep_encrypted": info.keep_encrypted
        })).collect();
        println!("{}", serde_json::json!({"content_id":xvd.content_id().to_string(),"files":rows}));
        return Ok(());
    }
    let out = Path::new(&args[2]).canonicalize()?;
    xodus::secrets::init_secrets().map_err(|_| "credential initialization failed")?;
    let tokens = xodus::tokens::TokenManager::with_keychain_and_memory();
    let client = reqwest::Client::builder().user_agent("xodus-cli/0.1.0").build()?;
    let (device_key, content_license) = license::get_license(&client, &tokens, xvd.content_id().to_string(), "neutral".into()).await
        .map_err(|_| "license acquisition failed")?;
    if content_license.content_keys.len()!=1 { return Err("expected one content key".into()); }
    let content_key=content_license.content_keys.into_values().next().ok_or("content key absent")?;
    let key=content_key.unpack(&device_key).map_err(|_| "content key unpack failed")?;
    for (name, mut info) in files {
        if !info.keep_encrypted { continue; }
        let normalized=name.replace('\\', "/");
        let relative=Path::new(&normalized);
        if relative.components().any(|c| !matches!(c, Component::Normal(_))) { return Err("unsafe output path".into()); }
        let target=out.join(relative);
        let parent=target.parent().ok_or("missing parent")?.canonicalize()?;
        if !parent.starts_with(&out) || target.is_symlink() { return Err("output path escapes destination".into()); }
        let temp=target.with_extension("decrypting");
        let mut output=tokio::fs::OpenOptions::new().write(true).create_new(true).open(&temp).await?;
        info.keep_encrypted=false;
        xvd.extract_file(&mut input, &mut output, &info, *key, |_,_|{}).await?;
        output.sync_all().await?;
        drop(output);
        tokio::fs::rename(&temp, &target).await?;
        println!("Decrypted {}", normalized);
    }
    Ok(())
}
