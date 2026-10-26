// GTOS-DSH-125 F1b: the request dispatcher the business path calls.
//
// The 124 test called `assertRequestUsesSelectedIdentity` directly and then counted processes machine-wide to argue that no
// child had started. That is wrong twice: the function is not the launch entry, and a machine-wide count cannot see a
// short-lived child and was itself a shell pipeline outside this packet's scope.
//
// This module is the entry. It admits the request, and only an admitted request reaches the transport. A refusal throws
// here, before `runBootStage` is called at all, so there is no launch to observe rather than a launch that did not happen.
//
// The evidence that a refusal prevented a launch is an observer attached to this call. The positive case proves the
// observer can see a launch, so a silent observer in the negative case means something.

import crypto from 'node:crypto';
import fs from 'node:fs';

const sha256 = (b) => crypto.createHash('sha256').update(b).digest('hex');

// The admission checks a request must pass. These are the same obligations the identity module states, applied at the
// entry rather than beside it.
export function admitRequest(request, identity, options) {
  const problems = [];
  const refuse = (obligation, why) => problems.push({ obligation, why });
  const host = options.host;

  if (!request || typeof request !== 'object') {
    return { admitted: false, problems: [{ obligation: 'request', why: 'no request' }],
      first_refusal: { obligation: 'request', why: 'no request' } };
  }
  if (!identity || identity.kind !== 'selected_image_identity') {
    refuse('identity', 'no selected identity was supplied');
  } else {
    if (request.identity !== identity.identity) refuse('identity', 'the request names a different identity');
    if (typeof request.image !== 'string' || !request.image.startsWith('/')) {
      refuse('path', 'the request carries no Linux image path');
    } else if (request.image !== identity.image.linux) {
      refuse('path', 'the request image path is not the selected path');
    }
    if (!request.image_pin) refuse('image', 'the request carries no image pin');
    else if (request.image_pin.sha256 !== identity.image.sha256
      || request.image_pin.bytes !== identity.image.bytes) {
      refuse('image', 'the image pin is not the selected image');
    }
    if (request.map_pin && identity.map
      && (request.map_pin.sha256 !== identity.map.sha256 || request.map_pin.bytes !== identity.map.bytes)) {
      refuse('map', 'the map pin is not the selected map');
    }
    // The bytes behind the approved path, read now. A caller who rewrote the file after selection is caught here.
    if (!problems.length) {
      let now = null;
      try { now = fs.readFileSync(host(request.image)); } catch { now = null; }
      if (now === null) refuse('image', 'the approved image path cannot be read');
      else if (sha256(now) !== identity.image.sha256) {
        refuse('image', 'the approved path carries different bytes than the selected identity');
      } else if (now.length !== identity.image.bytes) {
        refuse('image', 'the approved path carries a different size than the selected identity');
      }
    }
  }
  const admitted = problems.length === 0;
  return { admitted, problems, first_refusal: problems.length ? problems[0] : null };
}

// The dispatcher. A request that is not admitted never reaches the bridge, so the observer records no launch.
export async function dispatchRequest(options) {
  const { bridge, control, stage, kind, stageOptions, identity, request, host, observer } = options;
  const events = [];
  const record = (e) => { events.push(e); if (typeof observer === 'function') observer(e); };

  const admission = admitRequest(request, identity, { host });
  if (!admission.admitted) {
    const error = new Error('REQUEST REFUSED at ' + admission.first_refusal.obligation + ': '
      + admission.first_refusal.why);
    error.admission = admission;
    error.events = events;
    error.launched = false;
    throw error;
  }

  // Only now is the bridge asked to build a plan and hand it to the transport. The report path is per stage, because the
  // bridge requires a fresh one for each call.
  const callControl = { ...control, report_path: control.report_root + '/evidence/' + stage + '.json' };
  const result = await bridge.runBootStage(callControl, stage, kind,
    { ...stageOptions, observer: record });
  return { admission, result, events, launched: events.some((e) => e.event === 'launch') };
}
