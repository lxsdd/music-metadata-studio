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

        // Bound UTF-8 source bytes, not just JavaScript UTF-16 characters.
        const encoder = new TextEncoder();
        const bytes = value => encoder.encode(value).length;
        const validText = (value, limit) => {
            if (typeof value !== 'string' || !value ||
                value.length > limit || bytes(value) > limit ||
                /[\u0000-\u001f\u007f]/.test(value)) return false;
            for (let i = 0; i < value.length; ++i) {
                const code = value.charCodeAt(i);
                if (code >= 0xD800 && code <= 0xDBFF) {
                    const next = value.charCodeAt(++i);
                    if (!(next >= 0xDC00 && next <= 0xDFFF)) return false;
                } else if (code >= 0xDC00 && code <= 0xDFFF) return false;
            }
            return true;
        };
        const validDuration = value => {
            if (typeof value !== 'string' || value.length < 4 ||
                value.length > 24) return false;
            // Same units, millisecond precision and 1s..6h bounds as C++.
            const match = /^PT(?:(\d{1,4})H)?(?:(\d{1,4})M)?(?:(\d{1,4})(?:\.(\d{1,3}))?S)?$/.exec(value);
            if (!match || (!match[1] && !match[2] && !match[3]))
                return false;
            const fraction = match[4] ?
                Number(match[4]) * 10 ** (3 - match[4].length) : 0;
            const ms = Number(match[1] || 0) * 3600000 +
                       Number(match[2] || 0) * 60000 +
                       Number(match[3] || 0) * 1000 + fraction;
            return ms >= 1000 && ms <= 21600000;
        };
        const validArtists = value => {
            const name = item => typeof item === 'string' ? item :
                item && !Array.isArray(item) && typeof item === 'object' ?
                item.name : null;
            const credits = Array.isArray(value) ? value : [value];
            return credits.length > 0 && credits.length <= 32 &&
                credits.every(item => validText(name(item), 1024));
        };
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
                (Array.isArray(type) && type.length <= 8 && type.some(matches))) {
                // JSON-LD must independently attest the CURRENT page identity.
                // Without url/@id this might be an unrelated recommendation.
                const identities = [item.url, item['@id']].filter(
                    value => value !== undefined && value !== null);
                if (!identities.length ||
                    identities.some(value => value !== url)) return;
                if (!validText(item.name, 2048) ||
                    !validDuration(item.duration) ||
                    !validArtists(item.byArtist)) return;
                found.push(item);
            }
        };
        for (const node of document.querySelectorAll('script[type]')) {
            if (node.getAttribute('type')?.trim().toLowerCase() !==
                'application/ld+json') continue;
            if (node.textContent.length > 65536 ||
                bytes(node.textContent) > 65536) continue;
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
        if (payload.length > 65536 || bytes(payload) > 65536)
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
