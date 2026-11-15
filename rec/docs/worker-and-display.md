# Worker And Display

> Current Unicode update: The two plate lines are now labeled Characters and Numbers and rendered with Unicode GDI text through `renderTextLine`, rather than Hershey text. See [Unicode reference](unicode-and-multilingual.md).

[Documentation index](README.md)

Thread ownership, bounded buffering, error propagation, and separate visual fields.

## `car::Worker::Worker`

```cpp
Worker(const Options& options)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Create a dedicated pipeline and start asynchronous inference.

**Inputs:** The same model/backend/recognition options used by `Pipeline`.

**Returns / output:** A running worker with an empty one-slot pending queue.

**Behavior, ownership, and failure:** Pipeline construction completes before the background thread starts. Model-load failures propagate to the constructing thread. The worker owns its thread and must outlive submit/take calls.

## `car::Worker::~Worker`

```cpp
~Worker()
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Stop and join the inference thread.

**Inputs:** No explicit arguments.

**Returns / output:** No return value; releases owned synchronization/model state after thread completion.

**Behavior, ownership, and failure:** Locks the queue, sets the stop flag, notifies the condition variable, and joins. A current inference finishes; pending frames need not be processed. Destruction can therefore block until inference returns. Do not destroy from the worker thread itself or while other threads call its methods.

## `car::Worker::submit`

```cpp
void Worker::submit(const cv::Mat& image, int frame, double timestamp)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Publish the newest frame without building an unbounded backlog.

**Inputs:** Frame image; frame sequence number; timestamp in seconds.

**Returns / output:** No return value.

**Behavior, ownership, and failure:** Under the mutex, deep-copies the image, replaces any pending frame, stores metadata, and wakes the worker. Increments the atomic dropped counter if an unprocessed pending frame was overwritten. That counter does not include every possible application-level frame skip. Producer need not retain the source image.

## `car::Worker::take`

```cpp
bool Worker::take(Result& result)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Consume the most recently completed result without waiting for a new inference.

**Inputs:** `result`: caller-owned output object.

**Returns / output:** True when a result was copied; false when none is available.

**Behavior, ownership, and failure:** Mutex-protected; clears the availability flag after consumption. If the worker recorded an exception, throws `std::runtime_error` with its message. The application performs result-age filtering separately. Intended for one consuming main thread.

## `car::Worker::loop`

```cpp
void Worker::loop()
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Run the background inference loop.

**Inputs:** No explicit parameters; uses owned queue and pipeline.

**Returns / output:** No direct return data; publishes `Result` objects and errors.

**Behavior, ownership, and failure:** Private thread entry point. Waits until stop or a frame is pending, removes the pending image, unlocks during inference, measures inference seconds, then publishes the result under lock. A newer completed result can replace an unconsumed one. Catches `std::exception`, stores its message, and exits; does not auto-restart.

## `car::Worker::dropped`

```cpp
int Worker::dropped() const
```

**Source:** [include/Car.hpp](../include/Car.hpp).

Read the pending-frame overwrite counter.

**Inputs:** No arguments.

**Returns / output:** An atomic integer snapshot.

**Behavior, ownership, and failure:** Inline accessor. Safe to read while the worker runs; does not reset the counter or count results rejected as stale by `run`.

## `car::draw`

```cpp
void draw(cv::Mat& image, const Tracker& tracker, double fps)
```

**Source:** [src/Car.cpp](../src/Car.cpp).

Draw tracking IDs, vehicle classes, separate text fields, trails, and preview-rate text.

**Inputs:** Mutable BGR frame, current tracker, and measured/scheduled preview FPS.

**Returns / output:** No return value; modifies the supplied image.

**Behavior, ownership, and failure:** Draws tracks with at least two vehicle observations. Labels explicit observed, unconfirmed, historical, or not_observed status. Characters and Numbers are shown only when stable and currently supported by a recent matching plate observation; otherwise they say not currently read. Panel bounds are clipped, and text is skipped below 80 pixels wide or 60 pixels high. Empty frames return. Boxes/trails use OpenCV clipping. Receives the read-only projected tracker view and does not perform inference or change track state.

Current behavior: [plate evidence and tracking fixes](evidence-and-tracking.md) define timestamp-aware updates, current/history labeling, safe overlays, evidence crops, test-only heuristics and 55-check validation.

Current identity continuity: [persistent tracking](persistent-tracking.md) supersedes deletion/restart descriptions. Lost identities are archived for appearance-supported recovery; current visibility and plate evidence remain separate.
