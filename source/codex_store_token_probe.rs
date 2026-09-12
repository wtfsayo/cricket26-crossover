use std::time::Duration;
use xodus::models::{live::ExchangeUserTokenOutcome, secrets::Token, soap};
use xodus::tokens::TokenManager;

async fn run() -> Result<usize, &'static str> {
    xodus::secrets::init_secrets().map_err(|_| "keychain")?;
    let tokens = TokenManager::with_keychain_and_memory();
    let Token::Legacy(device) = tokens.get_device_sts_token().map_err(|_| "device_credentials")? else { return Err("device_shape"); };
    let Token::Legacy(user_sts) = tokens.get_user_sts_token().map_err(|_| "user_credentials")? else { return Err("user_shape"); };
    let user = tokens.get_user().map_err(|_| "selected_store_account")?;
    let client = reqwest::Client::builder().connection_verbose(false)
        .redirect(reqwest::redirect::Policy::none()).timeout(Duration::from_secs(12))
        .build().map_err(|_| "client")?;
    let device_response = xodus::api::live::exchange_device_token(&client, device.clone(),
        "{d6d5a677-0872-4ab0-9442-bb792fce85c5}".into(), "www.microsoft.com".into(),
        Some(soap::PolicyReference::mbi_ssl())).await.map_err(|_| "device_exchange")?;
    let Token::Compact(device_token) = Token::from(device_response) else { return Err("device_response"); };
    let user_response = xodus::api::live::exchange_user_token(&client, user_sts, user.username,
        device, None, Some("Silent".into()), "{d6d5a677-0872-4ab0-9442-bb792fce85c5}".into(),
        &[("www.microsoft.com".into(), Some(soap::PolicyReference::mbi_ssl()))])
        .await.map_err(|_| "user_exchange")?;
    let issued = match user_response {
        ExchangeUserTokenOutcome::Issued(soap::BodyContent::RequestSecurityTokenResponseCollection(c)) => c.security_tokens.into_iter().next().ok_or("empty_user_response")?,
        ExchangeUserTokenOutcome::Issued(soap::BodyContent::RequestSecurityTokenResponse(t)) => *t,
        _ => return Err("user_exchange_rejected"),
    };
    let Token::Compact(user_token) = Token::from(issued) else { return Err("user_response"); };
    let request = xodus::models::licensing::LicenseTokenRequest {
        parent_product_id: "9N6JF50HZFW9".into(), enforce_sellable_by: true,
        related_product_ids: vec!["9N6JF50HZFW9".into()],
        custom_developer_string: format!("Cricket26-local-diagnostic-{}", uuid::Uuid::new_v4()),
        beneficiaries: vec![xodus::models::licensing::LicenseUserIdentity {
            identity_type: "Msa".into(), identity_value: user_token, local_ticket_reference: user.puid,
        }],
    };
    let mut response = client.post("https://licensing.mp.microsoft.com/v8.0/licenseToken")
        .header("from", "XboxLicenseManager").header("Authorization", device_token)
        .header("user-agent", "XboxLm-PC/Microsoft.GamingServices_32.107.4002.0_x64__8wekyb3d8bbwe")
        .json(&request).send().await.map_err(|_| "license_endpoint")?;
    println!("license_http_status={}", response.status().as_u16());
    if !response.status().is_success() { return Err("license_http_rejected"); }
    let mut body = Vec::new();
    while let Some(chunk) = response.chunk().await.map_err(|_| "license_read")? {
        if body.len() + chunk.len() > 65535 { return Err("license_response_size"); }
        body.extend_from_slice(&chunk);
    }
    let parsed = serde_json::from_slice::<xodus::models::licensing::LicenseTokenResponse>(&body);
    body.fill(0);
    let token = parsed.map_err(|_| "license_response_schema")?.license_token;
    if token.is_empty() || token.len() > 49152 { return Err("invalid_license_response"); }
    Ok(token.len())
}

#[tokio::main]
async fn main() {
    // Existing exchange helpers may panic with server material; never print it.
    std::panic::set_hook(Box::new(|_| eprintln!("probe_error=internal_exchange_failure")));
    match tokio::time::timeout(Duration::from_secs(35), run()).await {
        Ok(Ok(size)) => println!("server_license_token_received=true token_bytes={size}"),
        Ok(Err(stage)) => { println!("server_license_token_received=false failed_stage={stage}"); std::process::exit(2); }
        Err(_) => { println!("server_license_token_received=false failed_stage=timeout"); std::process::exit(124); }
    }
}
