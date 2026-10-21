// Browser bookmarklet must never access credentials or private Crates APIs.
// Execute the exact shipping script against minimal fake DOM fixtures.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const js=fs.readFileSync(
    path.join(__dirname,'../scripts/crates-copy-selected-beatport-link.js'),
    'utf8');

const beatport=(id)=>'https://www.beatport.com/track/extended-mix/'+id;
function anchor(href,visible=true) {
    return {
        getAttribute:(key)=>key==='href'?href:null,
        getClientRects:()=>visible?[{}]:[]
    };
}
async function scenario({host='crates.co',all=[],row=null}={}) {
    let copied='',message='';
    const dom={
        querySelectorAll:(s)=>{
            assert.equal(s,'a[href]');
            return all;
        },
        activeElement:{closest:()=>row},
    };
    if(row) row.querySelectorAll=(s)=>{assert.equal(s,'a[href]');return row.items};
    const context={
        location:{hostname:host,protocol:'https:',href:'https://crates.co/'},
        URL,document:dom,
        window:{getSelection:()=>({isCollapsed:true})},
        navigator:{clipboard:{writeText:async value=>{copied=value;}}},
        alert:msg=>{message=msg;},
    };
    const result=vm.runInNewContext(js,context,{timeout:1000});
    await result;
    return {copied,message};
}
(async()=>{
    let r=await scenario({all:[anchor(beatport(321))]});
    assert.equal(r.copied,beatport(321));
    assert.match(r.message,/One exact Beatport URL copied/);
    r=await scenario({all:[anchor(beatport(321)),anchor(beatport(322))]});
    assert.equal(r.copied,'');
    assert.match(r.message,/More than one/);
    r=await scenario({all:[anchor(beatport(321),false)]});
    assert.equal(r.copied,'');
    r=await scenario({host:'evil-crates.co',all:[anchor(beatport(321))]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor('https://evil.org/track/x/321')]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor('https://www.beatport.com/track/mix/321?session=abc')]});
    assert.equal(r.copied,'');
    r=await scenario({
        all:[anchor(beatport(321)),anchor(beatport(322))],
        row:{items:[anchor(beatport(322))]},
    });
    assert.equal(r.copied,beatport(322));
    assert.doesNotMatch(js,/\bfetch\s*\(/);
    assert.doesNotMatch(js,/document\.cookie\s*[;=\[]/);
    console.log('PASS: Crates visible single-link browser helper identity/safety');
})().catch(err=>{console.error(err);process.exitCode=1;});
