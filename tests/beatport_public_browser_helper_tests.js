// Execute the shipping Beatport browser helper without real network/browser access.
// Browser import must attest exact recording identity, never guess from page context.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const {TextEncoder}=require('node:util');
const script=fs.readFileSync(
    path.join(__dirname,'../scripts/beatport-copy-public-jsonld.js'),'utf8');
const url='https://www.beatport.com/track/home/321';
const good=()=>({
    '@type':'MusicRecording',url,name:'Home (Extended Mix)',
    byArtist:{name:'DJ Example'},duration:'PT6M42S'
});
async function scenario({recordings=[good()],types=['application/ld+json'],
                         host='www.beatport.com',port='',protocol='https:',
                         pathname='/track/home/321',username='',password='',
                         search='',hash='',origin=null}={}) {
    const alerts=[],writes=[];
    const scripts=recordings.map((record,i)=>({
        textContent:JSON.stringify(record),
        getAttribute:key=>key==='type'?(types[i]||types[0]):null,
    }));
    const ctx={
        location:{hostname:host,port,protocol,pathname,username,password,
                  search,hash,
                  origin:origin ?? protocol+'//'+host+(port?':'+port:'')},
        document:{
            querySelectorAll:query=>{
                assert.equal(query,'script[type]');
                return scripts;
            },
            createElement:()=>{throw Error('Fallback should not run');},
        },
        navigator:{clipboard:{writeText:async text=>writes.push(text)}},
        alert:msg=>alerts.push(msg),
        TextEncoder,
    };
    await vm.runInNewContext(script,ctx,{timeout:1500});
    return {writes,alerts};
}
(async()=>{
    let r=await scenario();
    assert.equal(r.writes.length,1);
    assert.match(r.writes[0],/^@beatport-public-jsonld\n@url=https:\/\/www\.beatport\.com\/track\/home\/321\n/);
    assert.equal(JSON.parse(r.writes[0].split('\n').slice(2).join('\n')).url,url);
    r=await scenario({recordings:[{...good(),url:undefined}]});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[{...good(),url:undefined,'@id':url}]});
    assert.equal(r.writes.length,1);
    r=await scenario({recordings:[{...good(),url:undefined,
        '@id':'https://www.beatport.com/track/other/322'}]});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[{...good(),'@id':
        'https://www.beatport.com/track/other/322'}]});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[good(),good()]});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[{'@graph':[
        {'@type':'WebPage'},good()]}]});
    assert.equal(r.writes.length,1);
    r=await scenario({recordings:[{...good(),
        '@type':['Thing','https://schema.org/MusicRecording']}],
        types:['APPLICATION/LD+JSON']});
    assert.equal(r.writes.length,1);
    r=await scenario({recordings:[good()],types:['application/javascript']});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[good()],host:'evil.beatport.com'});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[good()],port:'8443'});
    assert.equal(r.writes.length,0);
    r=await scenario({recordings:[good()],protocol:'http:'});
    assert.equal(r.writes.length,0);
    // Browser provenance must not silently remove URL parameters,
    // fragments, or credentials when constructing the clipboard locator.
    for (const unsafe of [
        {search:'?ref=other'}, {hash:'#alternate'},
        {username:'visitor'}, {password:'example'},
        {origin:'https://unrelated.example'},
    ]) {
        r=await scenario({recordings:[good()],...unsafe});
        assert.equal(r.writes.length,0);
        assert.match(r.alerts[0],/canonical Beatport track page/);
    }
    r=await scenario({recordings:[good()],pathname:'/track/home/321/'});
    assert.equal(r.writes.length,0);

    // Do not say "copied" for JSON-LD that foobar would reject.
    for(const duration of ['PT0.999S','PT7H','PT1.1234S',
                           'P1D','PT','PT1M2M','PT0S']) {
        r=await scenario({recordings:[{...good(),duration}]});
        assert.equal(r.writes.length,0,'bad ISO duration: '+duration);
    }
    for(const byArtist of [null,[],{name:''},[{name:''}],{name:'\u0001'}]) {
        r=await scenario({recordings:[{...good(),byArtist}]});
        assert.equal(r.writes.length,0,'bad artist identity');
    }
    // One valid artist cannot hide malformed or excess collaborator data.
    for(const byArtist of [
        [{name:'DJ Example'},{name:''}],
        [{name:'DJ Example'},{name:'Second\nArtist'}],
        [{name:'DJ Example'},{}],
        [{name:'DJ Example'},null],
        Array(33).fill({name:'Artist'}),
    ]) {
        r=await scenario({recordings:[{...good(),byArtist}]});
        assert.equal(r.writes.length,0,'all collaboration credits must qualify');
    }
    r=await scenario({recordings:[{...good(),byArtist:[
        {name:'DJ Example'},{name:'DJ Example'}]}]});
    assert.equal(r.writes.length,1,'duplicate source credits remain visible');
    r=await scenario({recordings:[{...good(),duration:'PT6M42.125S',
        byArtist:[{name:'DJ Example'},{name:'Partner'}]}]});
    assert.equal(r.writes.length,1,'qualified fractional duration and credits');
    for (const name of ['A'.repeat(2049),'Broken\uD800','Track\nInjected']) {
        r=await scenario({recordings:[{...good(),name}]});
        assert.equal(r.writes.length,0,'invalid UTF-8/title input');
    }
    r=await scenario({recordings:[{...good(),
        '@type':['MusicRecording',...Array(8).fill('Other')]}]});
    assert.equal(r.writes.length,0,'unbounded type array');
    r=await scenario({recordings:[{...good(),name:'Hören'}]});
    assert.equal(r.writes.length,1,'valid Unicode title');
    // JS .length undercounts UTF-8 bytes, especially for non-ASCII data.
    r=await scenario({recordings:[{...good(),notes:'漢'.repeat(23000)}]});
    assert.equal(r.writes.length,0,'oversized UTF-8 page cannot be imported');
    assert.match(r.alerts[0],/No unique MusicRecording/);

    assert.doesNotMatch(script,/\bfetch\s*\(/);
    assert.doesNotMatch(script,/document\.cookie\s*[;=\[]/);
    assert.doesNotMatch(script,/localStorage|sessionStorage/);
    console.log('PASS: Beatport public-page browser helper identity/transport constraints');
})().catch(e=>{console.error(e);process.exitCode=1;});
