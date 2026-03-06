# Third-Party Libraries and Licenses

This document lists all external libraries used in Beerlang, along with their licenses and copyright information.

---

## 1. Mini-GMP

**Purpose:** Arbitrary precision arithmetic (bigint support)

**Location:** `lib/mini-gmp.c`, `lib/mini-gmp.h`

**Version:** From GNU GMP (included subset)

**License:** Dual-licensed under LGPL v3+ or GPL v2+

**Copyright:**
```
Copyright 1991-1997, 1999-2019 Free Software Foundation, Inc.
Contributed to the GNU project by Niels Möller
```

**License Text:**
```
The GNU MP Library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The GNU MP Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the GNU MP Library.  If not,
see https://www.gnu.org/licenses/.
```

**Homepage:** https://gmplib.org/
**Source:** https://gmplib.org/repo/gmp/file/tip/mini-gmp

---

## 2. microlog (ulog)

**Purpose:** Lightweight logging library for VM debugging and diagnostics

**Location:** `lib/ulog.c`, `lib/ulog.h`

**Version:** v7.0.0 (October 2025)

**License:** MIT License

**Copyright:**
```
Copyright (c) 2020 rxi (original log.c)
Copyright (c) 2025 Andrei Gramakov (microlog modifications and extensions)
```

**License Text:**
```
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

**Homepage:** https://github.com/an-dr/microlog
**Documentation:** https://github.com/an-dr/microlog/blob/main/README.md

---

## License Compatibility

All third-party libraries used in Beerlang are compatible with the project's goals:

- **Mini-GMP:** LGPL v3+ allows dynamic/static linking in proprietary software
- **microlog:** MIT license is permissive and compatible with any use case

Both libraries can be freely distributed with Beerlang without imposing licensing restrictions on Beerlang itself.

---

## Acknowledgments

We are grateful to the authors and maintainers of these libraries:
- The GNU GMP team and Niels Möller for Mini-GMP
- rxi for the original log.c
- Andrei Gramakov for microlog enhancements

Their work enables Beerlang to provide powerful features while maintaining a small, efficient footprint.
