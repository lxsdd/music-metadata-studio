// User-triggered on the ALREADY OPEN https://crates.co/ page.
// Copies exactly one visible Beatport track link from a focused/selected row
// or, only if unambiguous, from the current page. No API, network request,
// cookies, login tokens, hidden application state or extra browser software.
(async () => {
  const say = (msg) => alert('Crates → Beatport (read-only): ' + msg);
  try {
    if (location.protocol !== 'https:' ||
        !['crates.co', 'www.crates.co'].includes(location.hostname) ||
        (location.port && location.port !== '443'))
      throw new Error('Open Crates.co in your normal browser first.');
    const valid = (href) => {
      try {
        if (typeof href !== 'string' || !href.startsWith('https://www.beatport.com/track/')) return null;
        const link = new URL(href);
        if (link.href !== href) return null;
        if (link.protocol !== 'https:' ||
            link.hostname !== 'www.beatport.com' ||
            link.username || link.password || link.port ||
            link.search || link.hash ||
            !/^\/track\/[a-z0-9-]{1,125}\/[1-9][0-9]{0,11}$/.test(
              link.pathname)) return null;
        return link.href;
      } catch { return null; }
    };
    const visible = (anchor) => {
      if (!anchor.getClientRects().length) return false;
      // A layout rectangle is not enough: visibility:hidden and opacity:0
      // are still present in the DOM and must not become track evidence.
      // A hidden ancestor can be much farther away than a track row.
      // Traverse the bounded ancestor chain; fail closed if it is deeper.
      let current = anchor;
      let depth = 0;
      while (current && depth++ < 64) {
        const style = window.getComputedStyle?.(current);
        if (current.hidden || current.inert ||
            current.getAttribute?.('aria-hidden') === 'true' ||
            (style && (style.display === 'none' ||
                       style.visibility === 'hidden' ||
                       style.visibility === 'collapse' ||
                       (style.opacity !== '' && style.opacity != null &&
                        Number(style.opacity) === 0)))) return false;
        current = current.parentElement;
      }
      return !current;
    };
    const rowFrom = (element) => element?.closest?.(
      'tr,li,article,[role="row"],[data-track-id],.track,.track-row');
    // A DOM Selection can anchor on either a Text node or an Element.
    // Taking parentElement unconditionally jumps OUT of the intended row
    // when the endpoint itself is a track-row Element.
    const endpointElement = (node) =>
      node?.nodeType === 3 ? node.parentElement : node;
    // Text can be selected inside nested spans of one exact Beatport anchor.
    // A direct anchor choice is stronger than a row with multiple mixes.
    const anchorFrom = element =>
      element?.tagName?.toLowerCase() === 'a' ? element :
      element?.closest?.('a[href]') || null;
    let selected = null;
    let selectedAnchor = null;
    const selection = window.getSelection();
    if (selection && !selection.isCollapsed) {
      // The two ends of a text selection must belong to the SAME row.
      // Otherwise taking only its anchor could silently copy the wrong mix.
      const start = rowFrom(endpointElement(selection.anchorNode));
      const end = rowFrom(endpointElement(selection.focusNode));
      if (!start || start !== end)
        throw new Error('Select text within one exact track row only.');
      selected = start;
      const firstLink = anchorFrom(endpointElement(selection.anchorNode));
      const lastLink = anchorFrom(endpointElement(selection.focusNode));
      if (firstLink && firstLink === lastLink) selectedAnchor = firstLink;
    }
    // Exact visible anchor selection (including nested keyboard focus) wins
    // over a row with multiple mixes. Invalid Beatport links never fall back.
    const directBeatportUrl = anchor => {
      if (!anchor) return null;
      const href = anchor.getAttribute?.('href');
      let target = null;
      try { target = new URL(href); } catch { /* not an absolute URL */ }
      if (target?.hostname !== 'www.beatport.com') return null;
      const url = valid(href);
      if (!url || !visible(anchor))
        throw new Error('Selected Beatport link is hidden or noncanonical.');
      return url;
    };
    let focusedUrl = directBeatportUrl(selectedAnchor);
    if (!selected) {
      const focused = document.activeElement;
      focusedUrl = directBeatportUrl(anchorFrom(focused));
      if (!focusedUrl) selected = rowFrom(focused);
    }
    // A result row can contain one canonical track plus another mix with
    // a noncanonical Beatport track link. Filtering out the unsafe link
    // would make the first recording look uniquely selected. Ignore hidden
    // links and unrelated Beatport artist/label pages, not unsafe tracks.
    const find = (root) => {
      const hits = [];
      let unsafeTrack = false;
      for (const anchor of root.querySelectorAll('a[href]')) {
        if (!visible(anchor)) continue;
        const href = anchor.getAttribute('href');
        const qualified = valid(href);
        if (qualified) { hits.push(qualified); continue; }
        try {
          const target = new URL(href);
          if (target.hostname === 'www.beatport.com' &&
              target.pathname.startsWith('/track/')) unsafeTrack = true;
        } catch { /* unrelated or relative link */ }
      }
      if (hits.length && unsafeTrack)
        throw new Error('Visible Beatport links include a noncanonical ' +
          'track URL. Select one exact link or open Beatport manually.');
      return hits;
    };
    // Refuse ambiguity, rather than taking the first search result.
    const hits = focusedUrl ? [focusedUrl] :
      Array.from(new Set(find(selected || document)));
    if (hits.length !== 1)
      throw new Error(hits.length ? 'More than one Beatport track link found. ' +
        'Select text in just the desired track row and try again.' :
        'No visible canonical Beatport track URL found. ' +
        'Use Crates share/link controls or the Beatport page manually.');
    const value = hits[0];
    try {
      await navigator.clipboard.writeText(value);
    } catch {
      const field = document.createElement('textarea');
      field.value = value;
      field.style.position = 'fixed';
      field.style.left = '-9999px';
      document.body.appendChild(field);
      field.select();
      const copied = document.execCommand('copy');
      field.remove();
      if (!copied) throw new Error('Clipboard permission denied.');
    }
    say('One exact Beatport URL copied. In Music Metadata Studio use ' +
        'Load Beatport URL (paste it if the query field is not empty). ' +
        'No Crates/Beatport credentials were read.');
  } catch (error) {
    say(error.message);
  }
})();
