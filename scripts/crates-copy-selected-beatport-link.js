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
        const link = new URL(href, location.href);
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
      let current = anchor;
      for (let depth = 0; current && depth < 12; ++depth) {
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
      return true;
    };
    const rowFrom = (element) => element?.closest?.(
      'tr,li,article,[role="row"],[data-track-id],.track,.track-row');
    let selected = null;
    const selection = window.getSelection();
    if (selection && !selection.isCollapsed) {
      // The two ends of a text selection must belong to the SAME row.
      // Otherwise taking only its anchor could silently copy the wrong mix.
      const start = rowFrom(selection.anchorNode?.parentElement ||
                            selection.anchorNode);
      const end = rowFrom(selection.focusNode?.parentElement ||
                          selection.focusNode);
      if (!start || start !== end)
        throw new Error('Select text within one exact track row only.');
      selected = start;
    }
    if (!selected) selected = rowFrom(document.activeElement);
    const find = (root) => Array.from(
      root.querySelectorAll('a[href]')).filter(visible)
      .map(a => valid(a.getAttribute('href'))).filter(Boolean);
    // Refuse ambiguity, rather than taking the first search result.
    const hits = Array.from(new Set(find(selected || document)));
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
