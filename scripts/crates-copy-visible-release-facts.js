/* User-clicked bookmarklet on ONE visible Crates release page.
   No navigation, fetch, cookies, application state or credential access. */
(async () => {
  const report = message => alert('Crates visible release (read-only): ' + message);
  try {
    if (location.protocol !== 'https:' ||
        !['crates.co', 'www.crates.co'].includes(location.hostname) ||
        location.port || location.search || location.hash ||
        !/^\/release\/[a-z0-9-]{1,125}\/[1-9][0-9]{0,11}$/.test(location.pathname))
      throw Error('Open one canonical Crates release detail page first.');
    const id = location.pathname.slice(location.pathname.lastIndexOf('/') + 1);
    const visible = element => {
      if (!element || !element.getClientRects().length) return false;
      let node = element;
      for (let n = 0; node && n < 64; ++n, node = node.parentElement) {
        const style = getComputedStyle(node);
        if (node.hidden || node.inert || node.getAttribute('aria-hidden') === 'true' ||
            style.display === 'none' || style.visibility === 'hidden' ||
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
        throw Error('Invalid visible release field.');
      return value;
    };
    const optional = element => {
      const value = element?.innerText?.trim() || '';
      if (value && (value.length > 2048 || /[\u0000-\u001f\u007f]/.test(value)))
        throw Error('Invalid optional release field.');
      return value;
    };
    const link = one(document, '.profile.release h2.cratePicker a.itemLink[href]');
    if (link.hostname !== location.hostname || link.pathname !== location.pathname)
      throw Error('Visible release heading conflicts with page identity.');
    const album = text(link);
    const profile = one(document, '.profile.release');
    const catalogInfo = one(profile, '.catalogInfo');
    const label = text(one(catalogInfo, 'a[href]'));
    const infoRaw = catalogInfo.innerText || '';
    if (infoRaw.length > 2048 || /[\u0000-\u0009\u000b-\u001f\u007f]/.test(infoRaw))
      throw Error('Invalid visible catalog block.');
    const infoLines = infoRaw.split('\n').map(v => v.trim()).filter(Boolean);
    if ((infoLines.length !== 1 && infoLines.length !== 2) ||
        infoLines[0] !== label) throw Error('Unqualified visible catalog information.');
    const catalog = infoLines[1] || '';
    const beatportLink = one(profile, 'p.beatportLink a[href*="beatport.com/release/"]');
    const target = new URL(beatportLink.href);
    if (target.protocol !== 'https:' ||
        target.hostname !== 'www.beatport.com' ||
        target.username || target.password || target.port || target.hash ||
        target.pathname !== location.pathname ||
        [...target.searchParams.keys()].some(k =>
          !['a_aid', 'a_cid', 'utm_source', 'utm_medium', 'utm_content', 'utm_campaign'].includes(k)))
      throw Error('Visible Beatport release link has another identity.');
    const rows = [...document.querySelectorAll('table.tracks tr[data-trackid]')]
      .filter(visible);
    if (!rows.length || rows.length > 128)
      throw Error('Missing, too many or hidden release tracks.');
    const seen = new Set();
    const lines = [
      '@crates-public-release',
      '@url=https://' + location.hostname + location.pathname,
      '@beatport-url=https://www.beatport.com' + target.pathname,
      '@provider=crates',
      '@id=release:' + id,
      '@scope=edition',
      'ALBUM=' + album,
      'LABEL=' + label,
      ...(catalog ? ['CATALOGNUMBER=' + catalog] : []),
    ];
    for (const [i, row] of rows.entries()) {
      const trackId = row.getAttribute('data-trackid');
      if (!/^[1-9][0-9]{0,11}$/.test(trackId) || seen.has(trackId))
        throw Error('Duplicate or unqualified visible track identity.');
      seen.add(trackId);
      const cells = [...row.querySelectorAll(':scope > td')];
      if (cells.length !== 12 || text(cells[2]) !== String(i + 1))
        throw Error('Track ordinals or release row structure changed.');
      const trackLink = one(cells[3], 'a[href]');
      if (trackLink.hostname !== location.hostname ||
          !/^\/track\/[a-z0-9-]{1,125}\/[1-9][0-9]{0,11}$/.test(trackLink.pathname) ||
          trackLink.pathname.slice(trackLink.pathname.lastIndexOf('/') + 1) !== trackId)
        throw Error('Visible track link and row identity conflict.');
      const artists = [...cells[4].querySelectorAll('a[href]')].filter(visible).map(text);
      const remixers = [...cells[5].querySelectorAll('a[href]')].filter(visible).map(text);
      if (!artists.length || artists.length > 32 || remixers.length > 32)
        throw Error('Unqualified release track credit.');
      if (text(one(cells[6], 'a[href]')) !== label ||
          text(one(cells[7], 'a[href]')) !== album)
        throw Error('Release track belongs to another label or edition.');
      const key = optional(cells[8]);
      const bpm = optional(cells[9]);
      const duration = text(cells[10]);
      if ((bpm && !/^(?:[3-9][0-9]|[12][0-9]{2}|300)$/.test(bpm)) ||
          !/^(?:[0-9]{1,3}:[0-5][0-9]|[0-5]:[0-5][0-9]:[0-5][0-9])$/.test(duration))
        throw Error('Unqualified BPM or duration.');
      // The table Date column is deliberately omitted: its meaning is not qualified.
      lines.push(
        '@track',
        '@provider=crates',
        '@id=track:' + trackId,
        '@scope=recording',
        'RELEASE_ID=' + id,
        'TRACKNUMBER=' + String(i + 1),
        'TITLE=' + text(trackLink),
        ...artists.map(value => 'ARTIST=' + value),
        ...remixers.map(value => 'REMIXER=' + value),
        'DURATION=' + duration,
        ...(bpm ? ['BPM=' + bpm] : []),
        ...(key ? ['KEY=' + key] : []));
    }
    const output = lines.join('\n') + '\n';
    if (new TextEncoder().encode(output).length > 65536)
      throw Error('Release evidence exceeds 64 KiB.');
    await navigator.clipboard.writeText(output);
    report('Copied ' + rows.length + ' release tracks and catalog details. ' +
      'Import clipboard in Music Metadata Studio (Beatport + Crates). ' +
      'Source is unverified preview-only; no Date inference or writes.');
  } catch (error) { report(error.message); }
})();