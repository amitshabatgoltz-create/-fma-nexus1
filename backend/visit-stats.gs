/**
 * Visit statistics for the admin panel (weekly / monthly visitors, last 30
 * days, busiest hours). Add this file to the site's Apps Script project.
 *
 * Wiring, inside the existing doGet(e):
 *   1. In the branch that handles type === 'trackVisit', also call:
 *        logVisit_(e);
 *   2. Add a new branch next to the others:
 *        if (e.parameter.type === 'visitStats') return visitStats_(e);
 *   3. Deploy > Manage deployments > edit the current deployment >
 *      Version: New version > Deploy (the /exec URL stays the same).
 *
 * Visits are stored in a sheet named "Visits" (created on first use):
 * column A = time, column B = anonymous visitor id from the browser.
 */

const VISITS_SHEET_ = 'Visits';
const VISIT_STATS_KEY_ = 'fma-nexus1-7q2k9x5w'; // same key the admin panel already sends
const VISIT_TZ_ = 'Asia/Jerusalem';

function visitsSheet_() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  let sh = ss.getSheetByName(VISITS_SHEET_);
  if (!sh) {
    sh = ss.insertSheet(VISITS_SHEET_);
    sh.appendRow(['time', 'visitor']);
  }
  return sh;
}

function logVisit_(e) {
  const vid = String((e && e.parameter && e.parameter.vid) || '').slice(0, 40);
  visitsSheet_().appendRow([new Date(), vid]);
}

function visitStats_(e) {
  const out = (obj) => ContentService.createTextOutput(JSON.stringify(obj))
    .setMimeType(ContentService.MimeType.JSON);
  if (!e || e.parameter.key !== VISIT_STATS_KEY_) return out({ error: 'unauthorized' });

  const sh = visitsSheet_();
  const last = sh.getLastRow();
  const rows = last > 1 ? sh.getRange(2, 1, last - 1, 2).getValues() : [];

  const now = new Date();
  const dayKey = (d) => Utilities.formatDate(d, VISIT_TZ_, 'yyyy-MM-dd');
  const days = [];
  const byDay = {};
  for (let i = 29; i >= 0; i--) {
    const d = new Date(now.getTime() - i * 86400000);
    const k = dayKey(d);
    byDay[k] = { d: k, visits: 0, ids: {} };
    days.push(byDay[k]);
  }
  const weekStart = dayKey(new Date(now.getTime() - 6 * 86400000));
  const hours = new Array(24).fill(0);
  const week = { visits: 0, ids: {} };
  const month = { visits: 0, ids: {} };
  let first = null;

  rows.forEach(([t, vid]) => {
    if (!(t instanceof Date)) return;
    if (!first || t < first) first = t;
    const k = dayKey(t);
    const day = byDay[k];
    if (!day) return; // older than 30 days
    const id = vid || ('anon-' + t.getTime());
    day.visits++; day.ids[id] = 1;
    month.visits++; month.ids[id] = 1;
    if (k >= weekStart) { week.visits++; week.ids[id] = 1; }
    hours[Number(Utilities.formatDate(t, VISIT_TZ_, 'H'))]++;
  });

  const count = (o) => Object.keys(o).length;
  return out({
    week: { visitors: count(week.ids), visits: week.visits },
    month: { visitors: count(month.ids), visits: month.visits },
    days: days.map((x) => ({ d: x.d, visits: x.visits, visitors: count(x.ids) })),
    hours: hours,
    since: first ? Utilities.formatDate(first, VISIT_TZ_, 'yyyy-MM-dd') : null
  });
}
