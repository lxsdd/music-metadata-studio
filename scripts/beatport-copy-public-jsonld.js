// Browser-side, deliberately user-triggered on an ALREADY OPEN public page.
// Copies publicly embedded MusicRecording JSON-LD to the read-only foobar
// clipboard format. No fetch, credentials, cookies, automation, or bypass.
// Use only where you have the right to read the displayed public information.
(async () => {
    const fail = (message) => { throw new Error(message); };
    try {
        const path = location.pathname;
        const url = location.origin + path;
        if (location.protocol !== 'https:' ||
            location.hostname !== 'www.beatport.com' ||
            !/^\/track\/[a-z0-9-]{1,125}\/[1-9][0-9]{0,11}$/.test(path))
            fail('Open a public canonical Beatport track page first.');
        const found = [];
        const scan = (item) => {
            if (Array.isArray(item)) { item.forEach(scan); return; }
            if (!item || typeof item !== 'object') return;
            if (Array.isArray(item['@graph'])) item['@graph'].forEach(scan);
            const type = item['@type'];
            if (type === 'MusicRecording' ||
                (Array.isArray(type) && type.includes('MusicRecording'))) {
                // Prefer only the exact visible page, never a related track.
                if ((item.url && item.url !== url) ||
                    (item['@id'] && item['@id'] !== url)) return;
                if (typeof item.name !== 'string' ||
                    typeof item.duration !== 'string' ||
                    !item.byArtist) return;
                found.push(item);
            }
        };
        for (const node of document.querySelectorAll(
            'script[type="application/ld+json"]')) {
            if (node.textContent.length > 65536) continue;
            try { scan(JSON.parse(node.textContent)); }
            catch (e) { /* unrelated site markup is not useful */ }
        }
        if (found.length !== 1)
            fail('No unique MusicRecording JSON-LD found on this page. ' +
                 'The current site layout may not expose it; nothing was copied.');
        const candidate = { ...found[0], '@type': 'MusicRecording' };
        // The selected visible URL remains explicit even if JSON-LD omits it.
        // Never silently replace a conflicting embedded identity.
        if (candidate.url && candidate.url !== url)
            fail('Beatport page and recording URLs disagree.');
        candidate.url = url;
        const payload = '@beatport-public-jsonld\n@url=' + url +
                        '\n' + JSON.stringify(candidate);
        if (payload.length > 65536)
            fail('Source metadata is too large for bounded foobar import.');
        try {
            await navigator.clipboard.writeText(payload);
        } catch (err) {
            const field = document.createElement('textarea');
            field.value = payload;
            field.style.position = 'fixed';
            field.style.left = '-9999px';
            document.body.appendChild(field);
            field.select();
            const copied = document.execCommand('copy');
            field.remove();
            if (!copied) fail('Clipboard permission denied; nothing imported.');
        }
        alert('Beatport track facts copied. In Music Metadata Studio select ' +
              'Beatport (offline) and click Import Beatport. No HTTP request ' +
              'or tag write was made by this helper.');
    } catch (error) {
        alert('Beatport read-only page copy: ' + error.message);
    }
})();
