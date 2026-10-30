/**
 * Drop 02 orders: left / right / pair, size S / M / L, stock counted in gloves out of 10.
 * Add this file to the site's Apps Script project, next to Code.gs.
 *
 * Wiring, two lines in Code.gs:
 *   1. First line inside doGet(e):
 *        const d2get = drop02Get_(e); if (d2get) return d2get;
 *   2. First line inside doPost(e):
 *        const d2post = drop02Post_(e); if (d2post) return d2post;
 *   Both return null for every request that is not about Drop 02, so the rest
 *   of Code.gs keeps working exactly as before.
 * Then: Deploy > Manage deployments > edit > Version: New version > Deploy.
 *
 * Orders are stored in a sheet named "Drop02" (created on first use).
 * The admin password is read from Script Properties (ADMIN_KEY), see security.gs.
 */

const DROP02_SHEET_ = 'Drop02';
const DROP02_TOTAL_ = 10;                         // gloves in this drop
const DROP02_PRICE_ = { single: 219, pair: 379 };
const DROP02_SIDES_ = { left: 'שמאל', right: 'ימין', pair: 'זוג' };
const DROP02_SIZES_ = ['S', 'M', 'L'];
const DROP02_STATUSES_ = ['ממתין ליצירת קשר', 'נוצר קשר בוואטסאפ', 'שולם', 'בוטל'];
const DROP02_HEADERS_ = ['time', 'name', 'phone', 'agentCode', 'side', 'size', 'gloves', 'price', 'status'];

function drop02Out_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
}

function drop02Sheet_() {
  let ss = SpreadsheetApp.getActiveSpreadsheet();
  if (!ss) {
    const props = PropertiesService.getScriptProperties();
    const id = props.getProperty('drop02SpreadsheetId');
    ss = id ? SpreadsheetApp.openById(id) : SpreadsheetApp.create('FMA Drop 02');
    if (!id) props.setProperty('drop02SpreadsheetId', ss.getId());
  }
  let sh = ss.getSheetByName(DROP02_SHEET_);
  if (!sh) {
    sh = ss.insertSheet(DROP02_SHEET_);
    sh.appendRow(DROP02_HEADERS_);
  }
  return sh;
}

// Run once from the editor to create the sheet and grant permissions.
function setupDrop02() {
  drop02Sheet_();
}

function drop02Rows_() {
  const sh = drop02Sheet_();
  const last = sh.getLastRow();
  if (last < 2) return [];
  return sh.getRange(2, 1, last - 1, DROP02_HEADERS_.length).getValues()
    .map((r, i) => ({ row: i + 2, values: r }));
}

// Gloves already reserved by every order that was not cancelled.
function drop02Taken_() {
  return drop02Rows_().reduce((sum, r) => r.values[8] === 'בוטל' ? sum : sum + Number(r.values[6] || 0), 0);
}

function drop02AdminOk_(key) {
  if (typeof adminOk_ === 'function') return adminOk_(key);   // security.gs, with lockout
  const real = PropertiesService.getScriptProperties().getProperty('ADMIN_KEY');
  return !!real && key === real;
}

function drop02Get_(e) {
  const type = e && e.parameter ? e.parameter.type : '';
  if (type === 'stock2') {
    return drop02Out_({ drop: '02', total: DROP02_TOTAL_, remaining: Math.max(0, DROP02_TOTAL_ - drop02Taken_()) });
  }
  if (type === 'orders2') {
    if (!drop02AdminOk_(e.parameter.key)) return drop02Out_({ error: 'unauthorized' });
    return drop02Out_(drop02Rows_().map(r => {
      const v = r.values;
      return {
        row: r.row, timestamp: v[0] instanceof Date ? v[0].toISOString() : v[0],
        name: v[1], phone: v[2], agentCode: v[3], side: v[4], size: v[5],
        gloves: v[6], price: v[7], status: v[8]
      };
    }));
  }
  return null;
}

function drop02Post_(e) {
  let data;
  try { data = JSON.parse(e.postData.contents); } catch (err) { return null; }
  if (!data || (data.action !== 'order2' && data.action !== 'updateStatus2')) return null;

  if (data.action === 'updateStatus2') {
    if (!drop02AdminOk_(data.key)) return drop02Out_({ error: 'unauthorized' });
    const row = Number(data.row);
    if (!(row >= 2) || DROP02_STATUSES_.indexOf(data.status) < 0) return drop02Out_({ error: 'bad request' });
    drop02Sheet_().getRange(row, 9).setValue(data.status);
    return drop02Out_({ ok: true });
  }

  // New order
  const text = (v, max) => String(v == null ? '' : v).replace(/<[^>]*>/g, '').trim().slice(0, max);
  const name = text(data.name, 80);
  const phone = text(data.phone, 10);
  const agentCode = text(data.agentCode, 40);
  const side = DROP02_SIDES_[data.side];
  const size = DROP02_SIZES_.indexOf(data.size) >= 0 ? data.size : '';
  if (!name || !/^05[0-9]{8}$/.test(phone) || !side || !size) return drop02Out_({ error: 'missing fields' });
  const gloves = data.side === 'pair' ? 2 : 1;
  const price = data.side === 'pair' ? DROP02_PRICE_.pair : DROP02_PRICE_.single;

  const lock = LockService.getScriptLock();
  lock.waitLock(10000);
  try {
    if (DROP02_TOTAL_ - drop02Taken_() < gloves) return drop02Out_({ error: 'sold out' });
    drop02Sheet_().appendRow([new Date(), name, phone, agentCode, side, size, gloves, price, DROP02_STATUSES_[0]]);
  } finally {
    lock.releaseLock();
  }
  return drop02Out_({ ok: true });
}
