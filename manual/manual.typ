// The Carciofo manual. The general chapters are written here, the firmware
// chapters come from the README of every firmware in src/, collected into
// build/data.json by build.py.

#import "@preview/cmarker:0.1.10"

#let data = json("build/data.json")

#let cobalt = rgb("#1d4e9e")
#let cobalt-deep = rgb("#143b7c")
#let teal = rgb("#2e9e93")
#let terracotta = rgb("#c9663f")
#let verde = rgb("#5e8c4a")
#let limone = rgb("#f3c74f")
#let cream = rgb("#faf3e3")
#let cream-soft = rgb("#fffbf1")
#let ink = rgb("#22304a")
#let ink-soft = rgb("#51607d")
#let accents = (cobalt, teal, terracotta, verde)

#let accent = state("accent", cobalt)
#let serif = "Fraunces"
#let sans = "Work Sans"

#let maiolica(size: 21mm) = tiling(size: (size, size),
  image("assets/pattern.svg", width: size, height: size))
#let sawtooth = tiling(size: (13mm, 3.84mm),
  image("assets/sawtooth.svg", width: 13mm, height: 3.84mm))

#let diamond(fill: limone, size: 4pt) = box(rotate(45deg,
  rect(width: size, height: size, fill: fill)))

#let month(date) = {
  let (y, m, d) = date.split("-").map(int)
  datetime(year: y, month: m, day: d).display(
    "[day padding:none] [month repr:long] [year]")
}

#let markdown(text, base: none) = cmarker.render(text, scope: (
  image: (path, alt: none) => image(
    if base == none { path } else { "/" + base + "/" + path }, alt: alt),
))

// ---------------------------------------------------------------- styles

#set document(title: "Carciofo manual", author: "wgd modular")
#set text(font: sans, size: 9pt, fill: ink, lang: "en")
#set par(justify: false, leading: 0.68em, spacing: 1.05em)
#set page(
  paper: "a5",
  margin: (x: 15mm, top: 19mm, bottom: 18mm),
  fill: cream-soft,
  header: context {
    let page-no = here().page()
    let openers = query(heading.where(level: 1))
    if openers.any(h => h.location().page() == page-no) { return }
    let before = query(heading.where(level: 1).before(here()))
    set text(size: 7pt, fill: ink-soft)
    grid(columns: (1fr, auto), align: (left, right),
      text(font: serif, style: "italic",
        if before.len() > 0 { before.last().body }),
      text(font: serif, style: "italic", fill: cobalt)[Carciofo])
    v(-1.6mm)
    line(length: 100%, stroke: 0.4pt + limone)
  },
  footer: context align(center, box(width: 7mm, height: 7mm,
    align(center + horizon, {
      place(center + horizon, rotate(45deg,
        rect(width: 4.6mm, height: 4.6mm, fill: limone, radius: 0.4mm)))
      text(size: 7pt, weight: 600, fill: cobalt-deep,
        counter(page).display())
    }))),
)

#show link: set text(fill: cobalt)
#set strong(delta: 200)
#show raw: set text(font: "DejaVu Sans Mono", size: 0.9em)
#show raw.where(block: true): it => block(width: 100%, fill: cream,
  radius: 2mm, inset: (x: 3.5mm, y: 3mm), it)

#set list(body-indent: 0.6em, spacing: 0.8em)
#set enum(spacing: 0.8em)

#set table(stroke: none, inset: (x: 5pt, y: 4.2pt))
#show table.cell.where(y: 0): set text(fill: white, weight: 600)
#show table: it => {
  // Markdown tables come with auto columns, let the last one take the rest.
  if it.columns.len() > 1 and it.columns.last() == auto {
    table(columns: it.columns.slice(0, -1) + (1fr,), align: it.align,
      ..it.children)
  } else {
    context block(radius: 2mm, clip: true,
      stroke: 0.6pt + accent.get().lighten(55%),
      { set text(size: 8pt); it })
  }
}

// Colours a chapter: headings, tables and list markers take its accent.
#let themed(colour, body) = {
  accent.update(colour)
  set table(fill: (_, y) => if y == 0 { colour } else if calc.even(y) { cream })
  set list(marker: diamond(fill: colour, size: 3.2pt))
  body
}

#show heading: set text(font: serif)
#show heading.where(level: 1): it => {
  pagebreak(weak: true)
  context {
    let colour = accent.get()
    place(top + left, dx: -15mm, dy: -19mm, block(width: 148mm, {
      rect(width: 100%, height: 27mm, fill: maiolica(size: 15mm))
      v(-4.5mm)
      rect(width: 100%, height: 3.84mm, fill: sawtooth)
    }))
    v(17mm)
    block(below: 6mm, {
      text(size: 24pt, weight: 600, fill: colour.darken(15%), it.body)
    })
  }
}
#show heading.where(level: 2): it => context block(above: 1.6em, below: 0.8em,
  sticky: true, {
    diamond(fill: limone, size: 4.2pt)
    h(2.2mm)
    text(size: 12.5pt, weight: 600, fill: accent.get().darken(10%), it.body)
  })
#show heading.where(level: 3): it => block(above: 1.3em, below: 0.6em,
  sticky: true, text(size: 10pt, weight: 600, fill: ink, it.body))

// A boxed panel in the style of the flasher's plaques.
#let plaque(colour: cobalt, inset: 4mm, body) = block(
  width: 100%, fill: cream-soft, radius: 3mm, stroke: 1.1pt + colour,
  inset: 1.2mm, block(width: 100%, radius: 2.2mm,
    stroke: 0.5pt + limone, inset: inset, body))

// ----------------------------------------------------------------- cover

#page(margin: 0pt, header: none, footer: none, fill: none,
  background: rect(width: 100%, height: 100%, fill: maiolica()))[
  #place(bottom + left, rect(width: 100%, height: 3.84mm, fill: sawtooth))
  #place(top + right, dx: -13mm, dy: 16mm,
    block(stroke: 2pt + cream-soft, radius: 1.5mm, clip: true,
      image("assets/panel.png", height: 172mm)))
  #place(top + left, dx: 11mm, dy: 30mm, block(width: 80mm, {
    block(fill: cream-soft, radius: 6mm, stroke: 2pt + cobalt, inset: 1.6mm,
      block(width: 100%, radius: 4.6mm, stroke: 1pt + limone,
        inset: (x: 6mm, top: 8mm, bottom: 7mm), align(center, {
          image("assets/mark.svg", width: 15mm)
          v(1mm)
          text(font: serif, size: 31pt, weight: 650,
            fill: cobalt-deep)[Carciofo]
          v(-4mm)
          text(font: serif, size: 13pt, style: "italic", fill: ink-soft)[
            User manual]
          v(4mm)
          line(length: 30%, stroke: 0.6pt + limone)
          v(3mm)
          set text(size: 8pt, fill: ink-soft)
          [A 6 HP Daisy module by wgd modular, with every firmware it
            runs and how to play them.]
          v(4.5mm)
          box(fill: cream, radius: 2mm, inset: (x: 3.2mm, y: 2mm),
            stroke: 0.5pt + cobalt.lighten(60%),
            text(size: 7.5pt, weight: 600, fill: cobalt)[
              #if data.version != none [Release #data.version · ]
              #month(data.date)])
        })))
  }))
]

#counter(page).update(1)

// -------------------------------------------------------------- contents

#let firmware-ids = data.firmwares.map(fw => fw.id)

#page(header: none)[
  #v(4mm)
  #text(font: serif, size: 20pt, weight: 600, fill: cobalt-deep)[Contents]
  #v(4mm)
  #show outline.entry.where(level: 1): it => {
    let el = it.element
    if el.has("label") and str(el.label) in firmware-ids {
      pad(left: 5mm, top: -0.2mm, text(fill: ink-soft, it))
    } else {
      v(1.6mm)
      text(weight: 600, it)
    }
  }
  #outline(title: none, depth: 1, indent: 0pt)
  #v(1fr)
  #plaque(colour: verde, inset: 3.5mm)[
    #set text(size: 7.5pt, fill: ink-soft)
    This manual is put together from the firmware descriptions in the
    #link("https://github.com/" + data.repo)[carciofo-firmware] repository,
    so it always matches the firmwares it ships with. It was built from
    commit #raw(data.commit) of #month(data.date).
  ]
]

// ------------------------------------------------------------ the module

#themed(cobalt)[
= The module

#grid(columns: (31mm, 1fr), column-gutter: 6mm,
  image("assets/panel.png", height: 133mm),
  {
    set text(size: 8.5pt)
    set par(spacing: 0.85em)
    [Carciofo is a 6 HP Eurorack module around the Electro-Smith Daisy
      Seed, a small Arm Cortex-M7 board made for audio, and the successor
      of the Löwenzahnhonig. What it does is defined entirely by the
      firmware it runs, and you can flash a different one at any time.]
    v(1mm)
    let item(name, body) = block(below: 2.6mm, {
      text(font: serif, weight: 600, size: 9.5pt, fill: cobalt-deep, name)
      linebreak()
      body
    })
    item[P1 to P4][Four knobs, their job depends on the firmware.]
    item[CV1, CV2][Control voltage inputs for modulation, gates, clocks
      or triggers, ±5 V. An unpatched input sits in the middle of its
      range.]
    item[B1, B2][Two buttons, to switch modes, tap a tempo, freeze or
      reach a second layer of the knobs. A short and a long press often do
      different things.]
    item[LED][An RGB LED behind the front panel shows what the firmware
      is doing: its mode, a tempo, a trigger or a frozen buffer.]
    item[IN L, IN R][Stereo audio inputs.]
    item[OUT L, OUT R][Stereo audio outputs.]
  })

== Controls in general

All knobs and CV inputs are read as values between 0 and 1. Where a
firmware sums a CV input with a knob, the knob sets the starting point and
the CV moves it from there. Some firmwares give the knobs a second layer
while a button is held. They use soft pickup: after switching layers a knob
only takes over once it is turned through the value it last had, so
nothing jumps.

Audio runs at 48 kHz in stereo. Every firmware chapter lists its controls
in a table, followed by what the LED tells you.
]

// -------------------------------------------------------------- flashing

#themed(teal)[
= Flashing a firmware

The quickest way to change the firmware is the web flasher, which runs in
your browser and needs nothing installed.

#let flasher = "wgd-modular.github.io/carciofo-firmware"
+ Open #link("https://" + flasher + "/", flasher) in Google Chrome or
  Microsoft Edge. Safari and Firefox do not support WebUSB, which the
  flasher needs.
+ Connect the Daisy Seed on the back of the module to your computer with a
  USB cable.
+ Hold the *BOOT* button on the Daisy Seed and tap *RESET* to enter the
  bootloader, then press *Connect* on the page and pick the device.
+ Choose a firmware and press *Flash*. It takes a few seconds, after which
  the module restarts with the new firmware.

The flasher always offers the firmwares of the latest release, together
with their controls. It can also flash a `.bin` file of your own, for
example one you built yourself.

== Building from source

The firmwares are open source and live in the
#link("https://github.com/" + data.repo)[carciofo-firmware] repository on
GitHub. Everything needed to build them is managed by
#link("https://pixi.sh")[pixi]:

```sh
git clone --recursive https://github.com/wgd-modular/carciofo-firmware
cd carciofo-firmware
pixi run build doppio
```

This builds a single firmware into `src/doppio/build/doppio.bin`, without a
name all firmwares are built. `pixi run manual` builds this manual.
]

// ------------------------------------------------------------- firmwares

#themed(verde)[
= Firmwares

#let count = data.firmwares.len()
#let words = ("No", "One", "Two", "Three", "Four", "Five", "Six", "Seven",
  "Eight", "Nine", "Ten", "Eleven", "Twelve")
#(if count < words.len() { words.at(count) } else { str(count) })
#(if count == 1 [firmware is] else [firmwares are]) part of this release.
Each has its own chapter, written by the people who made it.

#v(3mm)
#for (i, fw) in data.firmwares.enumerate() {
  let colour = accents.at(calc.rem(i, accents.len()))
  block(below: 4.2mm, breakable: false, grid(
    columns: (1fr, 8mm), column-gutter: 3mm,
    {
      text(font: serif, weight: 600, size: 11pt, fill: colour.darken(10%),
        link(label(fw.id), fw.title))
      linebreak()
      text(size: 8.3pt, fill: ink-soft, fw.summary)
    },
    align(right, text(font: serif, weight: 600, size: 11pt, fill: colour,
      context str(counter(page).at(label(fw.id)).first()))),
  ))
}
]

#for (i, fw) in data.firmwares.enumerate() {
  let colour = accents.at(calc.rem(i, accents.len()))
  themed(colour, {
    [#heading(level: 1, fw.title) #label(fw.id)]

    if fw.history != none {
      let h = fw.history
      let people = h.authors.map(a => {
        if a.login == none { a.name } else if a.login == a.name {
          link("https://github.com/" + a.login, "@" + a.login)
        } else {
          [#a.name (#link("https://github.com/" + a.login, "@" + a.login))]
        }
      })
      let key(body) = text(weight: 600, fill: colour, body)
      block(below: 5mm, {
        set text(size: 7.5pt, fill: ink-soft)
        grid(columns: (auto, 1fr), column-gutter: 3mm, row-gutter: 1.6mm,
          key[By], people.join(", "),
          key[Added], month(h.added),
          key[Last change], [#month(h.updated) in #link("https://github.com/"
            + data.repo + "/commit/" + h.commit, raw(h.commit))],
          key[Source], link("https://github.com/" + data.repo + "/tree/main/"
            + fw.path, raw(fw.path)),
        )
      })
    }

    if fw.intro != "" { markdown(fw.intro, base: fw.path) }

    for section in fw.sections {
      heading(level: 2, section.title)
      if lower(section.title) == "controls" {
        plaque(colour: colour, markdown(section.body, base: fw.path))
      } else {
        markdown(section.body, base: fw.path)
      }
    }
  })
}

// ------------------------------------------------------------ back cover

#page(margin: 0pt, header: none, footer: none, fill: none,
  background: rect(width: 100%, height: 100%, fill: maiolica()))[
  #place(top + left, rect(width: 100%, height: 3.84mm, fill: sawtooth))
  #place(center + horizon, block(width: 92mm,
    block(fill: cream-soft, radius: 5mm, stroke: 2pt + cobalt, inset: 1.6mm,
      block(width: 100%, radius: 3.6mm, stroke: 1pt + limone, inset: 7mm,
        align(center, {
          image("assets/mark.svg", width: 11mm)
          v(2mm)
          text(font: serif, size: 15pt, weight: 600,
            fill: cobalt-deep)[wgd modular]
          v(2mm)
          set text(size: 7.5pt, fill: ink-soft)
          [The firmwares and this manual are open source under the MIT
            license. Fraunces and Work Sans are licensed under the SIL Open
            Font License.]
          v(2mm)
          link("https://github.com/" + data.repo,
            "github.com/" + data.repo)
        })))))
]
