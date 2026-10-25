// Pure operation routing. The caller supplies the frozen policy and all current attempts.
// This validates declared identity; source review still has to establish that a helper does that operation.
const own = (value, key) => Object.prototype.hasOwnProperty.call(value, key);
const need = (value, message) => { if (!value) throw new Error('OPERATION BUDGET: ' + message); };
const record = value => value !== null && typeof value === 'object' && !Array.isArray(value);

export function operationBudget(policy, request, attempts) {
  need(record(policy) && policy.schema === 1 && typeof policy.task === 'string' && policy.task.length > 0,
    'frozen policy identity');
  need(record(policy.categories) && record(policy.operations), 'policy tables');
  const categories = Object.keys(policy.categories), operations = Object.keys(policy.operations), modes = new Set();
  need(categories.length > 0 && operations.length > 0, 'nonempty policy');
  for (const category of categories) {
    need(/^[a-z][a-z0-9_]*$/.test(category) && Number.isSafeInteger(policy.categories[category])
      && policy.categories[category] > 0 && policy.categories[category] <= 10000, 'category cap');
  }
  for (const id of operations) {
    const route = policy.operations[id];
    need(/^[a-z][a-z0-9_.]*$/.test(id) && record(route) && own(policy.categories, route.category), 'operation route');
    need(typeof route.mode === 'string' && /^[a-z][a-z0-9_.-]*$/.test(route.mode)
      && !modes.has(route.mode), 'one unique control mode per operation');
    modes.add(route.mode);
  }
  need(record(request) && request.task === policy.task && typeof request.operation === 'string'
    && own(policy.operations, request.operation), 'known operation in current task');
  const id = request.operation, route = policy.operations[id];
  need(request.category === route.category, 'operation cannot change category');
  need(record(request.control) && request.control.operation_id === id && request.control.mode === route.mode,
    'control mode and operation agree');
  need(Array.isArray(attempts), 'all attempt records required');
  let used = 0;
  for (const attempt of attempts) {
    need(record(attempt) && attempt.task === policy.task && typeof attempt.complete === 'boolean'
      && typeof attempt.operation_id === 'string' && own(policy.operations, attempt.operation_id),
      'history identity, including incomplete attempts');
    const previous = policy.operations[attempt.operation_id];
    need(attempt.operation_mode === previous.mode && attempt.category === previous.category,
      'history route disagreement');
    if (previous.category === route.category) used += 1;
  }
  const cap = policy.categories[route.category];
  need(used < cap, 'category exhausted: ' + route.category);
  return { operation_id: id, operation_mode: route.mode, category: route.category, cap,
    used_before: used, used_after: used + 1, remaining_after: cap - used - 1,
    counted_from: 'all current task attempt records; incomplete and failed attempts count', gate: 'passed' };
}
