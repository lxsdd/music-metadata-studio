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
            location.origin !== 'https://www.beatport.com' ||
            (location.port && location.port !== '443') ||
            // The visited URL must be canonical before it can be
            // represented as exact public-page provenance.
            location.username || location.password ||
            location.search || location.hash ||
            !/^\/track\/[a-z0-9-]{1,125}\/[1-9][0-9]{0,11}$/.test(path))
            fail('Open a public canonical Beatport track page first.');
        const found = [];
        const scan = (item, depth = 0) => {
            if (depth > 4 || found.length > 1) return;
            if (Array.isArray(item)) {
                if (item.length > 64) return;
                item.forEach(entry => scan(entry, depth + 1));
                return;
            }
            if (!item || typeof item !== 'object') return;
            if (Array.isArray(item['@graph']))
                scan(item['@graph'], depth + 1);
            const type = item['@type'];
            const matches = value => value === 'MusicRecording' ||
                value === 'https://schema.org/MusicRecording';
            if (matches(type) ||
                (Array.isArray(type) && type.some(matches))) {
                // JSON-LD must independently attest the CURRENT page identity.
                // Without url/@id this might be an unrelated recommendation.
                const identities = [item.url, item['@id']].filter(
                    value => value !== undefined && value !== null);
                if (!identities.length ||
                    identities.some(value => value !== url)) return;
                if (typeof item.name !== 'string' ||
                    typeof item.duration !== 'string' ||
                    !item.byArtist) return;
                found.push(item);
            }
        };
        for (const node of document.querySelectorAll('script[type]')) {
            if (node.getAttribute('type')?.trim().toLowerCase() !==
                'application/ld+json') continue;
            if (node.textContent.length > 65536) continue;
            try { scan(JSON.parse(node.textContent)); }
            catch (e) { /* unrelated site markup is not useful */ }
        }
        if (found.length !== 1)
            fail('No unique MusicRecording JSON-LD found on this page. ' +
                 'The current site layout may not expose it; nothing was copied.');
        const candidate = { ...found[0], '@type': 'MusicRecording' };
        // The recording already attested this exact canonical page URL.
        // Never replace a conflicting embedded identity.
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
