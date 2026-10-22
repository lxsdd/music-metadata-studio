// Execute the shipping Beatport browser helper without real network/browser access.
// Browser import must attest exact recording identity, never guess from page context.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
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
    assert.doesNotMatch(script,/\bfetch\s*\(/);
    assert.doesNotMatch(script,/document\.cookie\s*[;=\[]/);
    assert.doesNotMatch(script,/localStorage|sessionStorage/);
    console.log('PASS: Beatport public-page browser helper identity/transport constraints');
})().catch(e=>{console.error(e);process.exitCode=1;});
