# 🎧 SEGfault SOUNDboard

**SEGfault SOUNDboard** is a modular, low-level audio editor backend built entirely in C for the University of Sydney’s COMP2017/9017 Systems Programming course. The goal of this project is to demonstrate systems-level concepts like dynamic memory management, pointer-based data structures, and efficient manipulation of large datasets — all through the lens of editing raw PCM audio data.

At its core, the project provides a track-based audio model. Each `track` is a logical view over a shared audio buffer, where operations such as **writing**, **reading**, **deleting**, and **inserting** are supported. Unlike naïve approaches that copy or shift memory during every edit, SEGfault SOUNDboard uses a **shared backing store** model — meaning multiple segments can reference the same audio data in memory without duplication. This allows for complex audio manipulation while keeping the performance overhead minimal.

What makes this project especially unique is its support for **reference-based editing semantics**. For example, inserting a segment from one track into another creates a parent-child relationship. Edits made to one are reflected in the other, preserving consistency and allowing users to treat audio like structured, composable data.

In addition to editing, the project includes functionality to identify repeated segments (such as advertisements) using **cross-correlation**, providing a foundation for features like ad removal or pattern recognition in larger sound pipelines.

## ✨ Why This Matters

- 🔍 Editing audio in real time requires more than just reading and writing samples. Efficient data handling is critical, especially when edits are frequent and involve long tracks.
- 📦 SEGfault SOUNDboard mimics how professional digital audio workstations (DAWs) avoid unnecessary data copying by referencing and layering audio buffers.
- 🎯 The design adheres strictly to memory safety, clean dynamic allocation, and modularity — all of which are critical for building real-world C libraries.
- 🧠 This project reinforces key systems programming skills: pointer management, shared ownership, data structure design, I/O handling, and memory cleanup under complex relationships.

## 🚀 Project Capabilities

- Load and save WAV files (PCM, mono, 16-bit, 8000 Hz)
- Dynamically allocate and manage audio tracks
- Read/write samples at any position
- Efficiently delete audio segments without shifting memory
- Insert shared-backed audio segments into other tracks
- Identify repeated audio segments via normalized cross-correlation
- Maintain parent-child relationships between audio regions
- Support memory-safe teardown of all shared structures

Whether you're building a lightweight audio engine, exploring signal processing, or learning how to write clean C code that manipulates large datasets — SEGfault SOUNDboard offers a focused and educational experience in systems-level software engineering.

> The project was developed from scratch — no external libraries, no frameworks, no memory shortcuts. Just raw C, raw audio, and raw pointer discipline.
