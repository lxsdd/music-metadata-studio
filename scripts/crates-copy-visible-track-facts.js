// User-clicked bookmarklet on ONE already-open Crates track page.
// Reads only currently visible DOM text and links. No fetch, cookies,
// hidden application state, credentials, or automated navigation.
(async () => {
  const report = message => alert('Crates visible track (read-only): ' + message);
  try {
    if (location.protocol !== 'https:' ||
        !['crates.co', 'www.crates.co'].includes(location.hostname) ||
        location.port || location.search || location.hash ||
        !/^\/track\/[a-z0-9-]{1,125}\/[1-9][0-9]{0,11}$/.test(location.pathname))
      throw Error('Open one canonical Crates track detail page first.');
    const id = location.pathname.slice(location.pathname.lastIndexOf('/') + 1);
    const visible = element => {
      if (!element || !element.getClientRects().length) return false;
      let node = element;
      for (let n = 0; node && n < 64; ++n, node = node.parentElement) {
        const style = getComputedStyle(node);
        if (node.hidden || node.inert || node.getAttribute('aria-hidden') === 'true' ||
            style.display === 'none' ||
            style.visibility === 'hidden' ||
            (style.opacity !== '' && Number(style.opacity) === 0)) return false;
      }
      return !node;
    };
    const one = (root, selector) => {
      const found = [...root.querySelectorAll(selector)].filter(visible);
      if (found.length !== 1) throw Error('Ambiguous or missing visible ' + selector);
      return found[0];
    };
    const text = element => {
      const value = element.innerText?.trim();
      if (typeof value !== 'string' || !value || value.length > 2048 ||
          /[\u0000-\u001f\u007f]/.test(value))
        throw Error('Invalid visible track field.');
      return value;
    };
    const pageUrl = 'https://' + location.hostname + location.pathname;
    const row = one(document, 'table.tracks tr[data-trackid="' + id + '"]');
    const cells = [...row.querySelectorAll(':scope > td')];
    if (cells.length !== 11 || !visible(row))
      throw Error('Crates track table changed or is incomplete.');
    const trackLink = one(cells[2], 'a[href]');
    if (trackLink.pathname !== location.pathname ||
        trackLink.hostname !== location.hostname)
      throw Error('Selected Crates title belongs to another track.');
    const title = text(trackLink);
    const artists = [...cells[3].querySelectorAll('a[href]')].filter(visible).map(text);
    if (!artists.length || artists.length > 32) throw Error('Missing artist credits.');
    const remixers = [...cells[4].querySelectorAll('a[href]')].filter(visible).map(text);
    if (remixers.length > 32) throw Error('Unbounded remix credits.');
    const label = text(one(cells[5], 'a[href]'));
    const album = text(one(cells[6], 'a[href]'));
    const key = text(cells[7]);
    const bpm = text(cells[8]);
    const duration = text(cells[9]);
    if (!/^(?:[3-9][0-9]|[12][0-9]{2}|300)$/.test(bpm) ||
        !/^(?:[0-9]{1,3}:[0-5][0-9]|[0-5]:[0-5][0-9]:[0-5][0-9])$/.test(duration))
      throw Error('Unqualified BPM or duration.');
    const profile = one(document, '.profile.track');
    const profileLink = one(profile, 'a[href*="beatport.com/track/"]');
    const target = new URL(profileLink.href);
    if (target.protocol !== 'https:' ||
        target.hostname !== 'www.beatport.com' ||
        target.username || target.password || target.port ||
        target.hash || target.pathname !== location.pathname ||
        [...target.searchParams.keys()].some(k =>
          !['a_aid','a_cid','utm_source','utm_medium','utm_content','utm_campaign'].includes(k)))
      throw Error('Public Beatport link does not attest the same track.');
    const canonicalBeatport = 'https://www.beatport.com' + target.pathname;
    const mixElements = [...profile.querySelectorAll('h2 .subtitle')].filter(visible);
    if (mixElements.length > 1) throw Error('Ambiguous mix label.');
    const mix = mixElements.length ? text(mixElements[0]) : '';
    if (mix && !(
        title.endsWith(' (' + mix + ')') ||
        title.endsWith(' [' + mix + ']') ||
        title.endsWith(' - ' + mix) ||
        title.endsWith(' – ' + mix) ||
        title.endsWith(' — ' + mix)))
      throw Error('Visible title and mix version disagree.');
    const genreElements = [...profile.querySelectorAll('a.genreLink')].filter(visible);
    if (genreElements.length > 1) throw Error('Ambiguous genre.');
    const genre = genreElements.length ? text(genreElements[0]) : '';
    const lines = [
      '@crates-public-track',
      '@url=' + pageUrl,
      '@beatport-url=' + canonicalBeatport,
      '@provider=crates',
      '@id=track:' + id,
      '@scope=recording',
      'TITLE=' + title,
      ...artists.map(value => 'ARTIST=' + value),
      ...(mix ? ['VERSION=' + mix] : []),
      ...remixers.map(value => 'REMIXER=' + value),
      'DURATION=' + duration,
      'BPM=' + bpm,
      'KEY=' + key,
      'LABEL=' + label,
      'ALBUM=' + album,
      ...(genre ? ['GENRE=' + genre] : []),
    ];
    const output = lines.join('\n') + '\n';
    if (new TextEncoder().encode(output).length > 65536)
      throw Error('Source evidence exceeds 64 KiB.');
    await navigator.clipboard.writeText(output);
    report('Exact public track facts copied (' + id +
      '). In Music Metadata Studio select Beatport + Crates and Import clipboard. ' +
      'Comparison only; the visible Date is intentionally not treated as a qualified tag.');
  } catch (error) { report(error.message); }
})();