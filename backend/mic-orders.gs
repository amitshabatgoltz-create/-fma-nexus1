/**
 * Microphone reservations: the backend for mic/index.html (form) and mic/admin.html (orders app).
 *
 * This is a separate Apps Script project, not part of the glove site's Code.gs.
 * Setup (also in Hebrew in mic/SETUP.md):
 *   1. Create a new Google Sheet. Extensions > Apps Script. Paste this whole file instead of Code.gs.
 *   2. Project Settings (gear) > Script Properties > Add:
 *        ADMIN_KEY    = a long password for the orders app
 *        NOTIFY_EMAIL = the address that gets an email on every new reservation (optional)
 *   3. Run `setup` once from the editor and allow the permissions.
 *   4. Deploy > New deployment > Web app. Execute as: Me. Who has access: Anyone.
 *      Copy the /exec URL into mic/config.js.
 *   After any change to this file: Deploy > Manage deployments > edit > Version: New version > Deploy.
 */

const MIC_SHEET_ = 'Reservations';
const MIC_HEADERS_ = ['time', 'name', 'phone', 'qty', 'city', 'note', 'status'];
const MIC_STATUSES_ = ['חדש', 'נוצר קשר', 'שולם', 'נשלח', 'בוטל'];
const MIC_MAX_QTY_ = 5;

function out_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

function sheet_() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sh = ss.getSheetByName(MIC_SHEET_);
  if (!sh) {
    sh = ss.insertSheet(MIC_SHEET_);
    sh.appendRow(MIC_HEADERS_);
    sh.setFrozenRows(1);
  }
  return sh;
}

// Run once from the editor.
function setup() { sheet_(); }

function prop_(k) { return PropertiesService.getScriptProperties().getProperty(k); }

// Admin check with a lockout after 10 wrong passwords in 15 minutes.
function adminOk_(key) {
  const cache = CacheService.getScriptCache();
  const fails = Number(cache.get('micAdminFails') || 0);
  if (fails >= 10) return false;
  const real = prop_('ADMIN_KEY');
  if (real && key === real) return true;
  cache.put('micAdminFails', String(fails + 1), 900);
  return false;
}

// At most `max` requests of one kind per 10 minutes (Apps Script cannot see IPs).
function allow_(action, max) {
  const lock = LockService.getScriptLock();
  lock.waitLock(5000);
  try {
    const cache = CacheService.getScriptCache();
    const k = 'micRate_' + action;
    const n = Number(cache.get(k) || 0);
    if (n >= max) return false;
    cache.put(k, String(n + 1), 600);
    return true;
  } finally {
    lock.releaseLock();
  }
}

function clean_(v, max) {
  return String(v == null ? '' : v)
    .replace(/<[^>]*>/g, '')
    .replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F\u007F]/g, '')
    .trim()
    .slice(0, max);
}

function rows_() {
  const sh = sheet_();
  const last = sh.getLastRow();
  if (last < 2) return [];
  return sh.getRange(2, 1, last - 1, MIC_HEADERS_.length).getValues().map((v, i) => ({
    row: i + 2,
    time: v[0] instanceof Date ? v[0].toISOString() : String(v[0]),
    name: v[1], phone: String(v[2]), qty: Number(v[3] || 0), city: v[4], note: v[5], status: v[6]
  }));
}

function doGet(e) {
  const p = (e && e.parameter) || {};
  if (p.type === 'orders') {
    if (!adminOk_(p.key)) return out_({ error: 'unauthorized' });
    return out_({ orders: rows_().reverse(), statuses: MIC_STATUSES_ });
  }
  return out_({ ok: true });
}

function doPost(e) {
  let data;
  try { data = JSON.parse(e.postData.contents); } catch (err) { return out_({ error: 'bad request' }); }

  if (data.action === 'status') {
    if (!adminOk_(data.key)) return out_({ error: 'unauthorized' });
    const row = Number(data.row);
    if (!(row >= 2) || row > sheet_().getLastRow() || MIC_STATUSES_.indexOf(data.status) < 0) {
      return out_({ error: 'bad request' });
    }
    sheet_().getRange(row, 7).setValue(data.status);
    return out_({ ok: true });
  }

  if (data.action !== 'reserve') return out_({ error: 'bad request' });
  if (data.hp) return out_({ ok: true });                 // honeypot: bots fill it, people don't
  if (!allow_('reserve', 60)) return out_({ error: 'busy' });

  const name = clean_(data.name, 80);
  const phone = clean_(data.phone, 10);
  const qty = Math.floor(Number(data.qty));
  const city = clean_(data.city, 60);
  const note = clean_(data.note, 400);
  if (!name || !/^05[0-9]{8}$/.test(phone) || !(qty >= 1 && qty <= MIC_MAX_QTY_)) {
    return out_({ error: 'missing fields' });
  }

  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    // Phone written as text so the leading 0 stays.
    sheet_().appendRow([new Date(), name, "'" + phone, qty, city, note, MIC_STATUSES_[0]]);
  } finally {
    lock.releaseLock();
  }

  const to = prop_('NOTIFY_EMAIL');
  if (to) {
    try {
      MailApp.sendEmail(to, 'שריון חדש: ' + qty + ' מיקרופון' + (qty > 1 ? 'ים' : ''),
        'שם: ' + name + '\nטלפון: ' + phone + '\nכמות: ' + qty +
        (city ? '\nעיר: ' + city : '') + (note ? '\nהערה: ' + note : ''));
    } catch (err) { /* the reservation is saved even if the email fails */ }
  }
  return out_({ ok: true });
}
