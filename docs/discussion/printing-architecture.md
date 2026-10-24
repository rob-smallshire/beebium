# Design printer emulation architecture conversation

## Prompt:
17/10/2026, 22:03:54

Soon I will need to implement support for the BBC Micro printer port. This is essentially a buffered User VIA port A rendering it output only save for a few control lines. To exercise it, I'll need a peripheral, so a period-appropriate printer emulator seems interesting. Epson-FX80 was typical of the era. I personally owned a Panasonic KX-P1180. What's out there in the way of ESP/P emulation that is licensed in such a way that we can either use it directly or adapt it to our needs. Or is it simple enough that we should just implement a printer emulator ourselves? And what about output? Proxy a real host printer?  Part of me wonders if we should *only* redirect to a host printer, and then implement a host printer ESC/P driver that produces only PDF. In other words, I'm not sure where the ESC/P or FX80 emulation should live.

## Response:
17/10/2026, 22:04:02 · gpt-6

> **Searched 2 websites**
>
> 
>
> Worked for 15s

I think there's an opportunity here to do something rather more interesting than simply implement an ESC/P interpreter for Beebium.

My inclination would be to **emulate the BBC Micro's printer port faithfully, but treat printers as separate peripherals, with their own emulation and output backends.** That fits Beebium's existing architecture particularly well: a headless emulator with independently implemented clients and peripherals.

I'd also distinguish three things that are easily conflated\:

1. **The printer port\:** An electrical and timing interface, essentially an eight-bit parallel data bus with handshaking.
2. **The printer\:** A device implementing a particular command language, character set, graphics capabilities, and mechanical behaviour.
3. **The output destination\:** A PDF, an image, a physical printer, or even a simulated sheet of paper in a graphical window.

These are separate concerns, and keeping them separate would let you emulate both an Epson FX-80 and your Panasonic KX-P1180 without duplicating the BBC-side implementation.

## 1\. Where should printer emulation live?

I'd draw the boundary like this\:

**Beebium core**

6502 → 6522 VIA → printer port latch and handshaking

Port events / parallel interface

**Peripheral transport**

Strobe, data, ACK, BUSY, reset and timing

**Printer emulator**

FX-80 / ESC/P / KX-P1180 command interpreter

Rendered document / printer output

The important distinction is that the **printer emulator should consume a stream of printer data, not VIA register writes**\.

That stream could come from the BBC Micro's parallel printer port, a virtual serial connection, a file, or even a command-line tool. It also makes printer-language emulation independently testable, without running a BBC Micro at all.

There is a strong case for keeping the ESC/P interpreter out of libBeebium, although it could still be distributed as part of the Beebium application.

## 2\. Existing ESC/P emulators worth investigating

The good news is that there are existing implementations. The less good news is that the most substantial ones have copyleft licences.

[![Epson Printer, Model FX-80, circa 1983](https://images.openai.com/static-rsc-4/4h4fCe-ZjRCy7otVjJ2TfxLIuM__VUfOqDtZsN_pkljF3qede9TuDotgaPYuKszvxZKjZQxrOP_wNEUqsuep2nEIXCGL9bskM10hA5m2QrElNXv5ohhX5roejnPrw6sxhIT9CoB-8_y7N-qAmNKZpO3RfLELTnpTnlOWnhIKJZiZyCVy4mIlTjUfDvw5fDc6?purpose=fullsize)](https://www.thehenryford.org/collections/explore/artifact/150113)

**[EscaPy](https://github.com/ysard/escapy?utm_source=chatgpt.com) - Python, AGPL-3.0**

Probably the most directly relevant project. It interprets ESC/P and ESC/P2 and produces PDFs, with support for printer profiles, fonts, graphics and user-defined characters.

The author describes the first version as roughly 12,000 lines including documentation and tests. That's a useful indication that comprehensive ESC/P support isn't quite trivial. \[[GitHub - ysard/escapy: An advanced Python interpreter for ESC/P and ESC/P2 commands, efficiently and accurately converting your print workflows into precise vectorial PDFs.](https://github.com/ysard/escapy?utm_source=chatgpt.com); [escapy/README.md at master · ysard/escapy](https://github.com/ysard/escapy/blob/master/README.md?utm_source=chatgpt.com)\]

---

[!['Printer' Still Not Working in Linux. · Issue #1561 · joncampbell123/dosbox-x · GitHub](https://images.openai.com/static-rsc-4/zSabGm__CIQZKwCY9TMv3SyYb3eqNDdW972-7OmlJpRGULkfbIU5NsiOT9rEC202R77-lVQEtxdkzNynZMoTiapM77yXIpOnfUIxyI1COiiNBBbD9sekDVElhbgFulGYAkIPJnAg0rT5sLnaA_1z8Ac1Pp5R8lPPSWx5mlgnjUj6gnFjLRtuh8sSRn87J1P8?purpose=fullsize)](https://github.com/joncampbell123/dosbox-x/issues/1561)

**[DOSBox-X](https://github.com/joncampbell123/dosbox-x?utm_source=chatgpt.com) - C++, GPL-family**

Its virtual printer supports numerous ESC/P and ESC/P2 commands, including graphics, and can produce PNG, BMP and PostScript or send output to a Windows printer.

Its documentation explicitly lists Epson MX-80 compatibility, making it relevant to the earlier printer generation. Some commands, including user-defined fonts, are unsupported. \[[Setting up printing in DOSBox-X](https://dosbox-x.com/wiki/Guide%3ASetting-up-printing-in-DOSBox%E2%80%90X?utm_source=chatgpt.com); [Guide:Setting up printing in DOSBox‐X · joncampbell123/dosbox-x Wiki](https://github.com/joncampbell123/dosbox-x/wiki/Guide%3ASetting-up-printing-in-DOSBox%E2%80%90X/d6863ff0df8d64040d3ae1a29a8aabdc80c03535?utm_source=chatgpt.com)\]

I would investigate two more codebases before deciding\:

| Project | Why it's interesting | Licensing consideration |
|--------|--------|--------|
| [MAME](https://github.com/mamedev/mame?utm_source=chatgpt.com) | Hardware-oriented emulation and potentially useful printer-device models | Predominantly BSD-3-Clause, but individual files have their own licences |
| [86Box](https://github.com/86Box/86Box?utm_source=chatgpt.com) | PC parallel-port and printer emulation code | GPL-2.0; check individual files |

I haven't verified that either contains a sufficiently complete, reusable FX-80 interpreter, so I'd regard these as research leads rather than confirmed candidates.

### Licensing implications

If Beebium is intended to retain a permissive licence, there's a significant distinction between studying an existing implementation and incorporating its source code.

- **EscaPy\:** AGPL-3.0 is likely unattractive for directly incorporating into a permissively licensed application, though it could be supported as an independently installed external converter. The author also offers commercial licensing.
- **DOSBox-X\:** Its GPL-family licensing presents similar concerns for code reuse.
- **MAME\:** Potentially attractive if the relevant implementation is independently licensed under BSD-3-Clause or another compatible licence.

A separate process is architecturally useful, but isn't automatically a way around GPL obligations. Distribution, integration and the precise licences matter.

There is also a distinction between **ESC/P** and **ESC/POS**\. The latter is Epson's receipt-printer command language. A number of modern projects emulate ESC/POS, but they're not suitable substitutes for an FX-80 emulator.

## 3\. Is ESC/P simple enough to implement ourselves?

For an FX-80-class printer, I suspect the answer is **yes, provided we target a specific historical printer rather than attempting comprehensive ESC/P compatibility**\.

The command parser itself isn't particularly difficult. The complexity lies in faithfully representing the printer's state and its output.

An initial implementation could support\:

| Feature | Complexity |
|--------|--------|
| ASCII, CR, LF, FF, BS, TAB | Low |
| Margins and line spacing | Low |
| Expanded, condensed and emphasized text | Low-medium |
| Italic and underline | Low-medium |
| ESC/P bit-image graphics (`ESC K`, `ESC L`, `ESC Y`, `ESC Z`, `ESC *`\) | Medium |
| Character ROM and national character sets | Medium |
| User-defined character glyphs | Medium |
| Exact nine-pin printhead appearance | Medium |
| Mechanical timing and realistic printer state | Medium-high |

I would particularly prioritise **bit-image graphics**\. A printer emulator that handles text but not graphics will miss some of the most interesting BBC Micro software.

For rendering, there are two approaches.

**Logical rendering\:** Interpret the commands and render characters using host fonts, producing clean, searchable PDFs. This is what a modern document converter would naturally do.

**Physical rendering\:** Simulate the individual dots struck by the printhead, using authentic character bitmaps and printer resolution. This gives output that looks like a real dot-matrix printout.

[![�����{�\[�h�}�C�R������낤�I�i�W�O�W�O�A�y�W�O�}�V���ꂩ��a�`�r�h�b�܂ł���P��ł��Ȃ��܂��j](https://images.openai.com/static-rsc-4/EkzkMtuXqtxbG3Fqq46EvxYIiuX39M5jaLsBevCJqNIm1LBpHqo0SlPmqeFKuS2fjQAooTeEgN39t9jxzSzFoNP4JfxPjSpfuXtZ-UnEquFQEuzOqeOXVbPKZqzy7uWxfl61qrced-FonAeaIHn7M1fz754KHwW1fcOdD_D6u6-slpIaCq5lc2iwiANp0fVu?purpose=fullsize)](https://userweb.alles.or.jp/chunichidenko/cpm_mycom250.html)

Period dot-matrix text, where the dot pattern is part of the result.

[![La Scuola Open Source](https://images.openai.com/static-rsc-4/sykc-eINJBSp-DnkcZbAemAjKDCXCLVBV-YMQEQAld6lDXK4Jz3g5K9Oa-D_MKahH_XI9dndquF_Zm0JRHucPMAu9Cxclt5evjRTwxLGPf6mCYcStoUWH12YhxQdwOkRqUYtUbMHsynuNGXzUKZkquqYJfbGgpYdOy1s6CwSrA0pstwxEIE-1OfFOz4adZA6?purpose=fullsize)](https://lascuolaopensource.xyz/)

Bit-image graphics are an important part of period printer compatibility.

For Beebium, I'd lean towards physical rendering first, with logical rendering as a later alternative.

An intermediate representation consisting of dot placements, paper movements and page boundaries could support PNG, PDF and interactive previews. At printer resolution, a monochrome A4 page is quite manageable, and the PDF backend could preserve dots as vector marks or embed a compressed bitmap.

## 4\. What about simply forwarding to a host printer?

I think this is an excellent idea as an **additional peripheral**, but not as the sole implementation.

A physical printer that supports ESC/P could receive the byte stream directly, without any language translation. For a modern printer, however, forwarding raw ESC/P usually won't work: the host print system expects a supported document format.

You'd therefore have two paths\:

**Raw printer passthrough**

BBC printer port → raw bytes → compatible physical printer

---

**Rendered host printing**

BBC printer port → ESC/P interpreter → PDF → host printing system

On macOS, the second path could use Core Graphics for PDF generation and the standard printing APIs for sending the document to a printer.

One consideration is that a physical printer doesn't necessarily acknowledge data immediately. A raw passthrough peripheral needs to account for buffering, disconnection, BUSY and ACK. I wouldn't make emulated CPU timing depend directly on unpredictable host I/O latency.

## 5\. Where should the job boundaries be?

This is actually one of the more interesting problems.

The BBC Micro sends a continuous stream of bytes. It doesn't necessarily signal the beginning or end of a print job.

Form feed is a page boundary, but not necessarily a document boundary. A program might print three pages with delays between them, and the emulator shouldn't create three unrelated PDF files.

I'd support explicit controls\:

- **New document / Finish document** in the printer UI.
- Automatic page breaks on form feed.
- Optional idle-time job completion.
- A raw byte capture facility, independent of the renderer.

The raw capture is particularly useful because it makes print output reproducible. A saved `.prn` file becomes a regression test for the ESC/P interpreter, and can be replayed through different printer models.

## 6\. The implementation I would choose

I would approach this incrementally.

**Phase 1 - Printer port and raw capture**

Implement the BBC hardware faithfully, including the latch, CA1/CA2 handshaking and the relevant control/status signals. Begin with a virtual peripheral that acknowledges data and writes a raw byte stream to a file.

This is enough to test the port with BBC BASIC `VDU 2`, `VDU 3`, `*FX 5` and `*FX 6`, and to exercise timing-sensitive printer routines.

**Phase 2 - Independent ESC/P interpreter**

Start with FX-80 compatibility. Implement a streaming parser with explicit printer state and an output representation in printer coordinates.

Test it with captured BBC software output, published command specifications and EscaPy as a possible comparison implementation.

**Phase 3 - PDF and preview**

Implement PDF output and a macOS preview window. Keep page dimensions, print resolution and character ROM selection in the printer model rather than the renderer.

This would make it possible to add authentic printer models later.

**Phase 4 - Host printing and other printer models**

Add a host printing backend, raw passthrough, and eventually a Panasonic KX-P1180 personality, once its precise Epson-compatible command set and differences are established.

### One architectural refinement

Given Beebium's existing gRPC-based architecture, I would resist creating a printer-specific RPC API.

Instead, a generic peripheral attachment mechanism could expose printer-port data and handshaking events. The virtual printer could then be a separate process, just like your second processors, or an in-process peripheral for simpler deployments.

There is one important timing wrinkle: **don't put a synchronous gRPC round trip in the critical path of every printer-port handshake**\. A small local, deterministic handshake model can expose BUSY/ACK to the VIA while asynchronously delivering bytes to the printer service. That preserves cycle-level behaviour without requiring the external process to respond at emulated microsecond timescales.

The same abstraction could eventually support other external devices.

## My conclusion

I would not make the printer port exclusively a host-printer redirector. That conflates a BBC Micro hardware interface with a particular use of that interface, and makes it unnecessarily difficult to simulate the behaviour of an actual 1980s printer.

Instead, I would implement a minimal printer peripheral and develop the ESC/P interpreter independently, probably in C++ or Python initially.

**EscaPy is the most promising existing implementation to evaluate as a reference or external converter.** But if preserving a permissive licence is important, I suspect a clean implementation of the original FX-80 command subset will be a more satisfactory long-term solution.

And there's a rather appealing end result: running *View*, *Wordwise* or a BBC graphics package, choosing an Epson FX-80 in Beebium, and watching a virtual sheet of continuous stationery fill with authentic nine-pin dots, before exporting it to PDF or printing it on a modern laser printer.

That seems more interesting than just producing a PDF, without making the first implementation dramatically more complicated.

## Prompt:
17/10/2026, 22:08:24

Beebium is already GPL 3.0 itself.

## Response:
17/10/2026, 22:08:26 · gpt-6

Ah, that changes the calculation substantially. **GPL-3.0 code is a perfectly reasonable candidate for direct incorporation into Beebium**, so there's considerably less reason to implement ESC/P from scratch.

I'd revise the shortlist accordingly\:

- **EscaPy (AGPL-3.0)\:** Worth investigating seriously. AGPL-3.0 and GPL-3.0 are compatible for combining code, although the resulting combined work has additional AGPL obligations for the AGPL-covered parts. It's Python, so a separate printer service would be a natural fit.
- **DOSBox-X (GPL-family)\:** Particularly interesting because its printer emulation is written in C++. However, we'd need to check the exact licences of the relevant source files. GPL-2.0-only code cannot simply be incorporated into a GPL-3.0 project.
- **MAME (mostly BSD-3-Clause)\:** Still worth examining for hardware accuracy and character ROMs, but licensing is no longer its principal attraction.

This also changes my architectural preference somewhat.

**I'd now lean towards extracting or adapting an existing C++ printer implementation**, assuming DOSBox-X has sufficiently complete FX-80 emulation and compatible licensing. It could live in a separate Beebium peripheral library, independent of the emulation core, without requiring a separate process.

I would still keep the three interfaces separate: BBC printer-port hardware, ESC/P printer emulation, and document rendering.

The question I'd investigate next is whether DOSBox-X's printer implementation is reasonably self-contained, what its exact licence is, and how faithfully it models Epson's nine-pin graphics and character sets. Those answers would tell us whether adapting it is a weekend's work or a substantial undertaking.

And I wouldn't rule out EscaPy simply because it's Python. Given Beebium's existing separation between emulator and clients, a Python-based printer service could be quite elegant, particularly if its PDF output is already good.

## Prompt:
17/10/2026, 22:14:50

I agree about separating the printer port abstraction and the printer abstraction. All of the Beebium expansion ports a) support plug-in extension peripherals which communicate with a Port (like PrinterPort, which models the signals of the Beeb's physical port) and b) these extensions can optionally provide gRPC APIs.

I think a small extension module which connected Beebium's PrinterPort with a separate EscaPy-based printer emulator over a socket would be the way to go. The Beebium-plugin would essentially be the PrinterPort to IP converter.

## Response:
17/10/2026, 22:14:51 · gpt-6

Yes. That fits Beebium's existing peripheral architecture very naturally, and I think it gives you a particularly clean separation of responsibilities.

I'd go one step further: **make the PrinterPort-to-IP adapter completely ignorant of ESC/P**\. Its job is to expose a parallel printer interface over a socket. EscaPy is simply one possible consumer.

The architecture becomes\:

```text
BBC Micro
  6522 VIA
     │
  PrinterPort
     │
  PrinterPortNetworkAdapter   ← Beebium extension
     │
     │ TCP socket
     │
  Virtual Printer Service
     ├── ESC/P interpreter (EscaPy)
     ├── Printer model (FX-80, etc.)
     ├── PDF generation
     └── Print job management
```

There are a couple of design decisions worth considering.

### 1\. Byte stream or signal-level protocol?

For the first version, I would use a simple **bidirectional byte-stream protocol**, rather than transmit individual signal transitions over the network.

The Beebium adapter would implement the printer's parallel handshaking locally\:

- Observe the data latch and strobe.
- Capture each accepted byte.
- Generate BUSY and ACK transitions at appropriate emulated times.
- Buffer bytes for asynchronous transmission over TCP.

That avoids putting network latency into the emulated printer handshake.

The trade-off is that the adapter effectively emulates a small part of the printer's behaviour. In particular, it needs to model a finite buffer and backpressure if the remote service stops consuming data.

For an initial implementation, this is probably entirely acceptable.

Later, if you want to emulate unusual printers or investigate timing-sensitive software, you could introduce a more detailed protocol carrying control-line events.

### 2\. A protocol independent of EscaPy

I'd make the socket protocol simple enough that other programs can implement it.

For example, an initial protocol could be nothing more than a TCP stream of printer bytes, with connection establishment representing attachment of a printer.

This would immediately allow several useful consumers\:

- EscaPy, producing PDF.
- A raw byte capture utility.
- A bridge to a real parallel printer.
- A diagnostic printer that logs commands and their parameters.

One subtlety: TCP connection boundaries shouldn't automatically mean print-job boundaries. Disconnecting a client isn't equivalent to sending a form feed or finishing a document.

If you eventually need printer status, reset, or paper-out reporting, a small framed protocol would be preferable to a pure byte stream. But I wouldn't necessarily build that complexity immediately.

### 3\. The socket endpoint belongs to the peripheral configuration

I'd expect a Beebium configuration along these lines\:

```yaml
peripherals:
  printer:
    type: tcp-parallel-adapter
    host: localhost
    port: 9100
```

Port 9100 is conventionally associated with raw network printing, which makes it an appealing default, although you wouldn't have to adopt that convention.

One benefit is that **Beebium doesn't need to know whether the remote endpoint is an emulator, a real printer, or a print server**\.

### 4\. One caveat about EscaPy

EscaPy is fundamentally an ESC/P-to-PDF interpreter rather than a cycle-accurate printer emulator.

That's fine for your proposed design, but it means the Beebium adapter must provide plausible printer-port behaviour independently of EscaPy.

Also, check whether EscaPy's public interface supports incremental input. If it primarily operates on complete print jobs, the network service can simply spool incoming bytes and invoke EscaPy when a job is complete.

In fact, I'd initially prefer that approach. It keeps the service simple and makes captured print jobs trivially reproducible.

### An interesting consequence

This creates a reusable **network parallel-printer peripheral** for Beebium, not merely an ESC/P printer implementation.

That strikes me as exactly the right level of abstraction for the project.

The only aspect I'd be cautious about is calling it a generic *PrinterPort-to-IP* converter if it locally synthesises handshaking. Technically, it's a virtual printer-port adapter with its own buffering and flow-control behaviour, rather than a transparent electrical bridge.

But that's a useful abstraction, not a flaw. And for ordinary BBC Micro printing software, I suspect it will be entirely sufficient.

## Prompt:
17/10/2026, 22:15:23

Yes, the socket part has no need to know about what is being communicated.

## Response:
17/10/2026, 22:15:25 · gpt-6

Exactly. The socket adapter is simply a **transport for bytes**, with the PrinterPort-facing side responsible for the appropriate handshaking.

That leaves three independently testable components\:

- **PrinterPort\:** Models the BBC Micro's hardware signals and timing.
- **Socket adapter\:** Converts printer-port transactions into a TCP byte stream, with buffering and flow control.
- **EscaPy service\:** Consumes the stream, interprets ESC/P and produces documents.

The interesting consequence is that the socket adapter doesn't even need to know it's connected to a printer emulator. It could equally well connect to `netcat`, a raw capture process, or a physical printer bridge.

One detail I'd preserve is the distinction between **the byte stream and the lifetime of a connection**\. Neither a TCP disconnect nor a temporary lack of data necessarily denotes the end of a print job. That's entirely a concern for the consumer.

I'd be inclined to implement the adapter first, test it against a trivial TCP byte receiver, and only then tackle the EscaPy service. That gives you a useful, finished Beebium peripheral before writing any printer-specific software.

## Prompt:
17/10/2026, 22:16:19

I'm familiar with RFC2217 (I think I have the number right) which is essentially serial over IP. Is there an equivalent Centronics printer port over IP?

## Response:
17/10/2026, 22:16:26 · gpt-6

> **Searched 12 websites**
>
> 
>
> Worked for 6s

Yes, **RFC 2217** is the correct number. It extends Telnet to support remote serial-port control, including baud rate, parity, modem control signals and flow control.

There isn't, to my knowledge, a comparably established RFC for transparently transporting a Centronics parallel-port interface over TCP/IP.

There are, however, several related protocols and conventions worth distinguishing.

| Protocol | Purpose | Centronics signal fidelity |
|--------|--------|--------|
| **Raw TCP printing (port 9100)** | Sends printer data as an uninterpreted byte stream | None |
| **LPD/LPR (RFC 1179)** | Submits print jobs to a remote spooler | None |
| **IPP** | Network printing with job management and status | None |
| **IEEE 1284** | Defines parallel peripheral interfaces, including Centronics-compatible modes | Physical interface, not IP |
| **USB printer class** | Carries printer data and status over USB | Not a transparent Centronics bridge |

The closest practical equivalent to what you're describing is **raw TCP printing**, often called *JetDirect* or *AppSocket* printing.

It has a particularly appealing property for Beebium: it is completely agnostic about the printer language. An ESC/P interpreter, PostScript printer or raw capture program can all consume the same stream.

But unlike RFC 2217, it doesn't provide a standard way to manipulate or observe the original interface's control lines.

Interestingly, there *is* an [RFC 1318 — Definitions of Managed Objects for Parallel-printer-like Hardware Devices](https://www.rfc-editor.org/rfc/rfc1318?utm_source=chatgpt.com)\. It covers Centronics and similar interfaces, including status signals such as paper-out. But it's an SNMP management specification, not a data transport protocol. \[[RFC 1318: Definitions of Managed Objects for Parallel-printer-like Hardware Devices](https://www.rfc-editor.org/info/rfc1318/?utm_source=chatgpt.com)\]

Likewise, IEEE 1284.4 defines multiplexed logical channels and flow control over parallel interfaces, but not a standard Centronics-over-TCP protocol. \[[IEEE SA](https://standards.ieee.org/ieee/1284/1956/?utm_source=chatgpt.com)\]

### What I'd use for Beebium

Given the architecture we've settled on, I would use **raw TCP on port 9100**\.

The adapter can translate the BBC's printer-port transactions into bytes and handle the local handshaking. The remote service receives exactly the data that a physical printer would receive, with no printer-language interpretation in the adapter.

The one limitation worth acknowledging is that raw TCP has no way to communicate paper-out, offline or other printer status back to Beebium. If those become important, you could introduce a separate status channel or a small protocol extension.

But for your immediate goal of exercising the BBC printer port and connecting it to EscaPy, raw TCP seems the most useful choice. It also gives you interoperability with existing raw-printing infrastructure rather than creating a Beebium-specific protocol unnecessarily.

## Prompt:
17/10/2026, 22:18:36

Ok, good. And is EscaPy just a library type package or does it include any such application already?

## Response:
17/10/2026, 22:18:38 · gpt-6

> **Searched 3 websites**
>
> 
>
> Worked for a couple of seconds

**EscaPy is already a standalone command-line application**, as well as a Python package. However, it doesn't appear to include a TCP print server.

According to its [GitHub documentation](https://github.com/ysard/escapy?utm_source=chatgpt.com), it accepts raw ESC/P data from files or standard input and produces PDFs. \[[GitHub - ysard/escapy: An advanced Python interpreter for ESC/P and ESC/P2 commands, efficiently and accurately converting your print workflows into precise vectorial PDFs.](https://github.com/ysard/escapy?utm_source=chatgpt.com); [pyscape](https://pypi.org/project/pyscape/1.0.0/?utm_source=chatgpt.com)\]

Installation\:

```bash
pip install pyscape
```

Note the PyPI package name is `pyscape`, not `escapy`\.

For example\:

```bash
escapy --pins 9 -o output.pdf input.prn
```

Or via standard input\:

```bash
cat input.prn | escapy --pins 9 -o output.pdf -
```

The `--pins 9` option is particularly relevant to your FX-80 use case. It also supports continuous stationery, printer profiles, character sets and bit-image graphics.

### Connecting it to Beebium

You could almost immediately get a working TCP printer using `socat`\:

```bash
socat TCP-LISTEN:9100,reuseaddr,fork \
  EXEC:"escapy --pins 9 -o output.pdf -"
```

That is a useful proof of concept, although concurrent connections would contend for the same output filename.

There is an important limitation: **EscaPy is fundamentally a batch converter**\. It reads a complete stream and generates a PDF, rather than operating as a continuously running virtual printer.

Consequently, with the arrangement above, you'd generally need to close the TCP connection to finish the PDF. This is fine for an initial experiment, but not ideal for a persistent virtual printer.

### An interesting discovery

EscaPy's README explicitly mentions the Libre Printer project, which captures data from physical legacy printer interfaces for conversion by EscaPy.

That reinforces the architectural separation we've discussed: data acquisition and printer-language interpretation are already treated as independent concerns by EscaPy's author.

**My suggestion\:** Start with the unmodified EscaPy CLI and a tiny TCP listener that spools bytes to a temporary file. Once that works, investigate EscaPy's Python API to see whether it can support incremental rendering or whether completing a print job must remain an explicit operation.

There's probably no need to fork EscaPy at all for the first iteration.

## Prompt:
17/10/2026, 22:20:46

This is interesting, because of course the BBC Micro also supported serial-attached printers, so we can make a Beebium Printer as a Python application that supports connection from a Beebium serial port or from a Beebium printer port.

## Response:
17/10/2026, 22:20:47 · gpt-6

Yes\! And that suggests an even cleaner architecture: **a standalone virtual printer with interchangeable input transports**, completely independent of Beebium.

```text
       Beebium
   ┌────────────────────┐
   │                    │
   │  PrinterPort       │
   │       │            │
   │  TCP adapter       │
   │       │            │
   │  RS423 port        │
   │       │            │
   │  RFC 2217 adapter  │
   │       │            │
   └───────┼────────────┘
           │
           │ TCP connections
           ▼
   ┌────────────────────┐
   │  Beebium Printer   │
   │                    │
   │  Input transports  │
   │  ├─ Raw TCP :9100  │
   │  └─ RFC 2217       │
   │         │          │
   │         ▼          │
   │  ESC/P interpreter │
   │      EscaPy        │
   │         │          │
   │         ▼          │
   │    PDF output      │
   └────────────────────┘
```

The printer application would have no need to know whether bytes originated from the BBC's parallel printer port or its RS423 interface.

There is one important distinction: **serial and parallel connections have different flow-control semantics**, even though they deliver the same ESC/P data.

For parallel, the Beebium adapter handles Centronics-style handshaking. For serial, the printer application may need to support XON/XOFF or RTS/CTS, depending on the printer configuration and how the BBC's serial driver is configured. RFC 2217 is useful here because it can carry serial control information as well as data.

I'd separate the Python application into three components\:

- **Transport\:** Raw TCP, RFC 2217, or eventually a local file.
- **Printer engine\:** EscaPy and the selected printer personality, such as an FX-80.
- **Output\:** PDF files initially, perhaps a preview window or host printing later.

One subtle point is that a real printer has **one print mechanism regardless of how it is connected**\. I would therefore make the serial and parallel transports alternative configurations of a single printer instance, rather than allowing both to feed the same interpreter simultaneously. Multiple virtual printers could still run as separate instances.

This also makes it possible to test the printer application without Beebium\:

```bash
cat sample.prn | nc localhost 9100
```

And it opens up a nice possibility: the same Python application could serve as a virtual printer for actual BBC Micro hardware, provided you have an appropriate serial-to-network bridge.

I particularly like that the resulting application is useful independently of Beebium. It could become a small companion project in its own right: an authentic 1980s Epson-compatible printer, accessible over either a virtual Centronics interface or a virtual RS423 connection, producing modern PDF documents.

## Prompt:
17/10/2026, 22:23:24

Yes, I think a separate but related project is the way to go, actually. (A runnable printer server could be bundled with Beebium for convenience). A sort of Printium (I'm sure cleverer names are possible. Sadly Lithium it too clever and too common).

## Response:
17/10/2026, 22:23:26 · gpt-6

I rather like **Printium**\. It has the virtue of making the relationship with Beebium obvious, without implying that the printer is exclusive to Beebium.

There are a few other possibilities worth entertaining, though.

| Name | Character |
|--------|--------|
| **Printium** | Natural companion to Beebium; clear purpose |
| **Dotium** | Short, distinctive, suggests dot-matrix printing |
| **Pinarium** | Nine-pin printheads; vaguely scientific |
| **Imprint** | Elegant, but almost certainly heavily used |
| **Tractor** | A delightful reference to continuous-feed paper |
| **Dottium** | Dot-matrix plus the Beebium naming convention |
| **Formfeed** | Period-appropriate, technically meaningful |
| **Printhead** | Direct, descriptive, but generic |
| **Papyrum** | Paper, with a pseudo-Latin flavour |
| **Escapade** | A particularly good pun on ESC/P |

My favourites are **Printium**, **Dotium**, and **Escapade**\.

There's something appealing about *Escapade* for an ESC/P printer emulator, although it might suggest that the project is specifically tied to Epson's command language. *Printium* leaves open the possibility of emulating other printer families, including daisywheel printers or plotters.

And I think that's a meaningful distinction: the project you're describing isn't really an ESC/P-to-PDF converter. It's a **network-accessible virtual vintage printer**, with configurable transports, printer personalities and output destinations.

That could eventually include an Epson FX-80, Panasonic KX-P1180, or even an HP 7475A plotter, without changing the fundamental architecture.

One further thought: a standalone printer server could expose a tiny HTTP interface for managing jobs, selecting printer models, and retrieving PDFs, while accepting print data over raw TCP or RFC 2217. That would be useful even when running headless alongside Beebium.

Of the names, though, I think **Printium** is the strongest match for your existing project family. It sounds like a companion application rather than a utility script.

## Prompt:
17/10/2026, 22:25:03

Actually, I think I'll go for Printium for now. Partly because it isn't necessarily ESC/P specific. We would add support for Daisywheel or others too (Postscript?) behind the same interface.

## Response:
17/10/2026, 22:25:04 · gpt-6

**Printium it is.** I think the name works particularly well once you stop thinking of it as an ESC/P converter and start thinking of it as a general-purpose vintage printer emulator.

I'd make the central abstraction a **printer model**, with three independent layers\:

```text
Printium
├── Transports
│   ├── Raw TCP (port 9100)
│   ├── RFC 2217
│   └── File / stdin
│
├── Printer models
│   ├── Epson FX-80 (ESC/P)
│   ├── Panasonic KX-P1180
│   ├── Daisywheel printer
│   └── PostScript printer
│
└── Output
    ├── PDF
    ├── Raster image
    └── Host printing
```

One architectural distinction worth preserving is between a **printer language** and a **printer model**\.

For example, an FX-80 and KX-P1180 might share much of an ESC/P interpreter but have different character ROMs, supported commands, printable areas and mechanical characteristics. A daisywheel printer might use Diablo 630 control codes, while a PostScript printer would need a completely different interpreter.

That suggests something like\:

`Transport → Printer Model → Rendering Backend`

With printer models internally sharing language interpreters where appropriate.

PostScript is especially interesting because you could use Ghostscript rather than implement the language yourself. It would make Printium useful for emulating considerably more sophisticated printers without much additional interpreter development.

I'd also resist making PDF the *only* output representation. An intermediate page-rendering abstraction would allow dot-matrix output to retain its authentic appearance while PostScript could preserve vector graphics and text.

And the separation from Beebium is valuable: **Printium could serve any vintage computer emulator, or real hardware with an appropriate network adapter.** Beebium would merely be its first client.

It has the makings of a useful little project in its own right.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)