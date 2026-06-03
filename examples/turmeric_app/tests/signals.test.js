// Turmeric Reactive System — Unit Tests
// Tests the signal/computed/effect logic independent of WASM codegen.
// Run: node tests/signals.test.js

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

// =============================================================================
// JavaScript reimplementation of turmeric/src/signal.sf
// =============================================================================

let _tracker_deps = [];
let _tracking = false;
let _sub_id_counter = 0;

function start_tracking() {
  _tracking = true;
  _tracker_deps = [];
}

function stop_tracking() {
  _tracking = false;
  const deps = _tracker_deps;
  _tracker_deps = [];
  return deps;
}

function record_dep(sig) {
  if (_tracking) {
    _tracker_deps.push(sig);
  }
}

function next_sub_id() {
  _sub_id_counter += 1;
  return _sub_id_counter;
}

class Subscription {
  constructor(id, fn) {
    this.id = id;
    this.fn = fn;
    this.active = true;
  }
}

class Signal {
  constructor(initial) {
    this.value = initial;
    this.subs = [];
  }

  get() {
    record_dep(this);
    return this.value;
  }

  set(next) {
    this.value = next;
    this.notify();
  }

  update(fn) {
    this.set(fn(this.value));
  }

  notify() {
    for (let i = 0; i < this.subs.length; i++) {
      if (this.subs[i].active) {
        this.subs[i].fn();
      }
    }
  }

  subscribe(fn) {
    const id = next_sub_id();
    this.subs.push(new Subscription(id, fn));
    return id;
  }

  unsubscribe(id) {
    for (let i = 0; i < this.subs.length; i++) {
      if (this.subs[i].id === id) {
        this.subs[i].active = false;
      }
    }
  }
}

function signal(initial) {
  return new Signal(initial);
}

class Computed {
  constructor(compute) {
    this.compute_fn = compute;
    this.subs = [];
    this.recompute();
  }

  get() {
    record_dep(this);
    return this.value;
  }

  recompute() {
    start_tracking();
    this.value = this.compute_fn();
    const deps = stop_tracking();
    for (let i = 0; i < deps.length; i++) {
      deps[i].subscribe(() => {
        this.value = this.compute_fn();
        this.notify();
      });
    }
  }

  notify() {
    for (let i = 0; i < this.subs.length; i++) {
      if (this.subs[i].active) {
        this.subs[i].fn();
      }
    }
  }

  subscribe(fn) {
    const id = next_sub_id();
    this.subs.push(new Subscription(id, fn));
    return id;
  }

  unsubscribe(id) {
    for (let i = 0; i < this.subs.length; i++) {
      if (this.subs[i].id === id) {
        this.subs[i].active = false;
      }
    }
  }
}

function computed(compute) {
  return new Computed(compute);
}

class EffectHandle {
  constructor(sub_ids, deps) {
    this.sub_ids = sub_ids;
    this.deps = deps;
  }

  dispose() {
    for (let i = 0; i < this.deps.length; i++) {
      this.deps[i].unsubscribe(this.sub_ids[i]);
    }
  }
}

function effect(fn) {
  start_tracking();
  fn();
  const deps = stop_tracking();
  const sub_ids = [];
  for (let i = 0; i < deps.length; i++) {
    sub_ids.push(deps[i].subscribe(fn));
  }
  return new EffectHandle(sub_ids, deps);
}

// =============================================================================
// Tests
// =============================================================================

describe('Signal get/set', () => {
  it('returns initial value from get()', () => {
    const s = signal(42);
    assert.equal(s.get(), 42);
  });

  it('returns updated value after set()', () => {
    const s = signal(0);
    s.set(10);
    assert.equal(s.get(), 10);
  });

  it('update() applies a function to the current value', () => {
    const s = signal(5);
    s.update(v => v * 2);
    assert.equal(s.get(), 10);
  });
});

describe('Signal subscribers fire on set', () => {
  it('subscriber is called when signal is set', () => {
    const s = signal('hello');
    let called = 0;
    s.subscribe(() => { called++; });
    assert.equal(called, 0);
    s.set('world');
    assert.equal(called, 1);
  });

  it('subscriber is called on each set', () => {
    const s = signal(0);
    let called = 0;
    s.subscribe(() => { called++; });
    s.set(1);
    s.set(2);
    s.set(3);
    assert.equal(called, 3);
  });

  it('unsubscribed callback does not fire', () => {
    const s = signal(0);
    let called = 0;
    const id = s.subscribe(() => { called++; });
    s.unsubscribe(id);
    s.set(1);
    assert.equal(called, 0);
  });
});

describe('Computed tracks signal deps and recomputes', () => {
  it('computed returns derived value', () => {
    const a = signal(2);
    const b = signal(3);
    const sum = computed(() => a.get() + b.get());
    assert.equal(sum.get(), 5);
  });

  it('computed updates when dependency changes', () => {
    const a = signal(1);
    const doubled = computed(() => a.get() * 2);
    assert.equal(doubled.get(), 2);
    a.set(5);
    assert.equal(doubled.get(), 10);
  });

  it('computed updates when any dependency changes', () => {
    const x = signal(10);
    const y = signal(20);
    const total = computed(() => x.get() + y.get());
    assert.equal(total.get(), 30);
    x.set(5);
    assert.equal(total.get(), 25);
    y.set(100);
    assert.equal(total.get(), 105);
  });
});

describe('Computed.get() is reactive (records dep for outer effects)', () => {
  it('effect depending on computed re-runs when underlying signal changes', () => {
    const s = signal(3);
    const c = computed(() => s.get() + 1);
    let observed = 0;
    effect(() => { observed = c.get(); });
    assert.equal(observed, 4);
    s.set(9);
    assert.equal(observed, 10);
  });
});

describe('Effect runs immediately on creation', () => {
  it('effect fn is called once upon creation', () => {
    const s = signal(7);
    let ran = 0;
    effect(() => { s.get(); ran++; });
    assert.equal(ran, 1);
  });
});

describe('Effect re-runs when signal dep changes', () => {
  it('effect re-executes when its signal dependency is set', () => {
    const s = signal('a');
    let log = [];
    effect(() => { log.push(s.get()); });
    assert.deepEqual(log, ['a']);
    s.set('b');
    assert.deepEqual(log, ['a', 'b']);
    s.set('c');
    assert.deepEqual(log, ['a', 'b', 'c']);
  });
});

describe('Effect re-runs when computed dep changes', () => {
  it('effect re-runs through computed chain', () => {
    const base = signal(1);
    const derived = computed(() => base.get() * 10);
    let values = [];
    effect(() => { values.push(derived.get()); });
    assert.deepEqual(values, [10]);
    base.set(2);
    assert.deepEqual(values, [10, 20]);
    base.set(3);
    assert.deepEqual(values, [10, 20, 30]);
  });
});

describe('Multiple effects on same signal all fire', () => {
  it('two effects on one signal both execute', () => {
    const s = signal(0);
    let a = 0;
    let b = 0;
    effect(() => { a = s.get(); });
    effect(() => { b = s.get() * 2; });
    assert.equal(a, 0);
    assert.equal(b, 0);
    s.set(5);
    assert.equal(a, 5);
    assert.equal(b, 10);
  });

  it('three effects on one signal all fire', () => {
    const s = signal(1);
    let results = [0, 0, 0];
    effect(() => { results[0] = s.get(); });
    effect(() => { results[1] = s.get() + 1; });
    effect(() => { results[2] = s.get() + 2; });
    assert.deepEqual(results, [1, 2, 3]);
    s.set(10);
    assert.deepEqual(results, [10, 11, 12]);
  });
});

describe('Effect with if/else inside still works (simulates reactive_show)', () => {
  it('effect with conditional branch tracks deps in taken branch', () => {
    const visible = signal(true);
    const content = signal('hello');
    let rendered = '';
    effect(() => {
      if (visible.get()) {
        rendered = content.get();
      } else {
        rendered = '';
      }
    });
    assert.equal(rendered, 'hello');
    content.set('world');
    assert.equal(rendered, 'world');
    visible.set(false);
    assert.equal(rendered, '');
  });

  it('toggling condition re-runs effect', () => {
    const flag = signal(true);
    let branch = '';
    effect(() => {
      if (flag.get()) {
        branch = 'true-branch';
      } else {
        branch = 'false-branch';
      }
    });
    assert.equal(branch, 'true-branch');
    flag.set(false);
    assert.equal(branch, 'false-branch');
    flag.set(true);
    assert.equal(branch, 'true-branch');
  });
});

describe('Nested function calls inside effect still track deps (simulates reactive_class)', () => {
  it('deps read in a helper function called within effect are tracked', () => {
    const name = signal('Alice');
    const age = signal(30);

    function buildLabel() {
      return `${name.get()} (${age.get()})`;
    }

    let label = '';
    effect(() => { label = buildLabel(); });
    assert.equal(label, 'Alice (30)');
    name.set('Bob');
    assert.equal(label, 'Bob (30)');
    age.set(25);
    assert.equal(label, 'Bob (25)');
  });

  it('computed used inside helper inside effect still propagates', () => {
    const first = signal('Jane');
    const last = signal('Doe');
    const full = computed(() => `${first.get()} ${last.get()}`);

    function formatGreeting() {
      return `Hello, ${full.get()}!`;
    }

    let greeting = '';
    effect(() => { greeting = formatGreeting(); });
    assert.equal(greeting, 'Hello, Jane Doe!');
    first.set('John');
    assert.equal(greeting, 'Hello, John Doe!');
    last.set('Smith');
    assert.equal(greeting, 'Hello, John Smith!');
  });
});
