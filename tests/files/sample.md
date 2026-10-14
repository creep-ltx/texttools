# Markdown, the viewer

A *Markdown* viewer and **editor** that lives inside the terminal. It shows
`code`, ~~mistakes~~, ***both at once***, and [links](other.md) to follow.
Bare addresses work too: https://aminet.net/package/util/misc/Markdown and
<someone@example.com>. A hard break comes here  
and the line goes on. An escaped \*star\* stays a star.

Setext heading
--------------

## Lists

- one
- two with a long line that has to be wrapped because it simply goes on and on past the width
  - nested a
  - nested b
    1. deep one
    2. deep two
- three

1. first
2. second
10. tenth?

- [ ] a task to do
- [x] a task done

## Quotes

> A quote with **bold** inside,
continued lazily.
>
> > And a quote in a quote.

> [!WARNING]
> Pictures need CTerm.

## Code

```c
/* a comment */
int main(void) { return 0; }
```

    indented code
    second line

## Table

| Name | Size | Notes |
|:-----|-----:|:-----:|
| Ahiru | 120 KB | files |
| Sumi | 90 KB | the ink, with a `code` cell and a | pipe |
| Maki | 60 KB | |

***

A reference [link][ref] and a [shortcut] one, an image ![logo](logo.png "the logo").

![the picture](pic.png)

<p align="center"><img src="pic.png" width="80" alt="centred"></p>

<!-- a comment that never shows -->

Entities: &copy; &amp; &lt;tag&gt; &#169; &#x263A;

[ref]: https://example.com/ref "Ref"
[shortcut]: #lists
