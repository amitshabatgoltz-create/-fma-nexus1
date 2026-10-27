/**
 * Server-side protection for the site's Apps Script backend.
 *
 * 1. Admin password out of the code
 *    - Project Settings (gear icon) > Script Properties > Add property:
 *        ADMIN_KEY = a new, long password (not the old 'fma-nexus1-...' one,
 *        which is public in the site's git history).
 *    - In Code.gs, replace line 1
 *        const ADMIN_KEY = 'fma-nexus1-7q2k9x5w';
 *      with
 *        const ADMIN_KEY = PropertiesService.getScriptProperties().getProperty('ADMIN_KEY');
 *    - In the visits file, replace VISIT_STATS_KEY_ the same way.
 *    The admin panel now asks for this password at login and the server checks it.
 *
 * 2. Use the helpers below inside doGet/doPost (wiring depends on Code.gs):
 *    - adminOk_(key)       instead of  key === ADMIN_KEY   (locks out after 10 wrong tries)
 *    - allow_('order', 20) at the top of each public POST action (global rate limit)
 *    - clean_(text, 500)   on every text field before saving it
 *    - validPhone_(phone)  before saving an order
 *    - photoOk_(base64)    before saving a review photo
 *
 * 3. Deploy > Manage deployments > edit > Version: New version > Deploy.
 */

// Admin check with a lockout: after 10 wrong passwords in 15 minutes, every
// admin request is refused until the window passes.
function adminOk_(key) {
  const cache = CacheService.getScriptCache();
  const fails = Number(cache.get('adminFails') || 0);
  if (fails >= 10) return false;
  const real = PropertiesService.getScriptProperties().getProperty('ADMIN_KEY');
  if (real && key === real) return true;
  cache.put('adminFails', String(fails + 1), 900);
  return false;
}

// Global rate limit per action: at most `max` requests per 10 minutes.
// Apps Script cannot see the sender's IP, so this caps floods for everyone.
function allow_(action, max) {
  const lock = LockService.getScriptLock();
  lock.waitLock(5000);
  try {
    const cache = CacheService.getScriptCache();
    const k = 'rate_' + action;
    const n = Number(cache.get(k) || 0);
    if (n >= max) return false;
    cache.put(k, String(n + 1), 600);
    return true;
  } finally {
    lock.releaseLock();
  }
}

// Plain text only: strips tags and control characters, trims, caps the length.
// The site also escapes everything it displays; this is the second layer.
function clean_(v, maxLen) {
  return String(v == null ? '' : v)
    .replace(/<[^>]*>/g, '')
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '')
    .trim()
    .slice(0, maxLen || 500);
}

function validPhone_(phone) {
  return /^05[0-9]{8}$/.test(String(phone || ''));
}

// Review photos: images only, at most about 3 MB.
function photoOk_(base64, mime) {
  if (!base64) return true;
  if (!/^image\/(jpeg|png|webp|heic|heif)$/.test(String(mime || ''))) return false;
  return String(base64).length <= 4 * 1024 * 1024;
}

// A uniform error reply the site understands as a failure.
function errorOut_(message) {
  return ContentService.createTextOutput(JSON.stringify({ error: message || 'rejected' }))
    .setMimeType(ContentService.MimeType.JSON);
}
