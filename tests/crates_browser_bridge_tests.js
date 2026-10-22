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
function anchor(href,visible=true,style={}) {
    return {
        getAttribute:(key)=>key==='href'?href:
            key==='aria-hidden'?(style.ariaHidden || null):null,
        getClientRects:()=>visible?[{}]:[],
        computedStyle:style,
        hidden:style.hidden || false,
        inert:style.inert || false,
        parentElement:null
    };
}
async function scenario({host='crates.co',port='',all=[],row=null,selection=null}={}) {
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
        location:{hostname:host,port,protocol:'https:',href:'https://crates.co/'},
        URL,document:dom,
        window:{
            getSelection:()=>selection || {isCollapsed:true},
            getComputedStyle:(element)=>element.computedStyle || {},
        },
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
    // URL() can silently canonicalize links which Crates did not expose
    // as an exact absolute public Beatport track identity. Fail closed.
    for (const noncanonical of [
        '//www.beatport.com/track/mix/321',
        ' https://www.beatport.com/track/mix/321',
        'https://www.beatport.com/track/mix/321 ',
        'https://www.beatport.com:443/track/mix/321',
        'HTTPS://www.beatport.com/track/mix/321',
        'https://WWW.BEATPORT.COM/track/mix/321',
        'https://www.beatport.com/track/mix/0321',
        '/track/mix/321',
    ]) {
        r=await scenario({all:[anchor(noncanonical)]});
        assert.equal(r.copied,'', 'noncanonical source link: '+noncanonical);
        assert.match(r.message,/No visible canonical Beatport track URL/);
    }
    r=await scenario({all:[anchor('https://www.beatport.com/track/mix/321?session=abc')]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor('https://user:password@www.beatport.com/track/mix/321')]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor('https://www.beatport.com:8443/track/mix/321')]});
    assert.equal(r.copied,'');
    r=await scenario({port:'8443',all:[anchor(beatport(321))]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{visibility:'hidden'})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{opacity:'0'})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{display:'none'})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{opacity:'0.0'})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{ariaHidden:'true'})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{inert:true})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[anchor(beatport(321),true,{hidden:true})]});
    assert.equal(r.copied,'');
    // Deeply nested Crates DOM must not expose links inside a hidden
    // ancestor: the old 12-level walk missed this and copied false evidence.
    const nested=(count,ancestorStyle={})=>{
        const leaf=anchor(beatport(321));
        let node=leaf;
        for(let i=0;i<count;i++){
            node.parentElement={
                parentElement:null,computedStyle:{},
                getAttribute:()=>null,hidden:false,inert:false
            };
            node=node.parentElement;
        }
        node.computedStyle=ancestorStyle;
        return leaf;
    };
    r=await scenario({all:[nested(14,{visibility:'hidden'})]});
    assert.equal(r.copied,'');
    r=await scenario({all:[nested(14,{opacity:'0'})]});
    assert.equal(r.copied,'');
    // A deep but entirely visible link is allowed; excessive depth is
    // conservatively refused rather than treated as visibility proof.
    r=await scenario({all:[nested(60)]});
    assert.equal(r.copied,beatport(321));
    r=await scenario({all:[nested(65)]});
    assert.equal(r.copied,'');
    r=await scenario({
        all:[anchor(beatport(321)),anchor(beatport(322))],
        row:{items:[anchor(beatport(322))]},
    });
    assert.equal(r.copied,beatport(322));
    // Selecting across TWO result rows must not silently pick the anchor's mix.
    const first={items:[anchor(beatport(321))]};
    const second={items:[anchor(beatport(322))]};
    const nodeFor=(row)=>({nodeType:3,parentElement:{closest:()=>row}});
    r=await scenario({
        all:[...first.items,...second.items],row:first,
        selection:{isCollapsed:false,anchorNode:nodeFor(first),
                   focusNode:nodeFor(second)}
    });
    assert.equal(r.copied,'');
    assert.match(r.message,/one exact track row/);
    r=await scenario({
        all:[...first.items,...second.items],row:second,
        selection:{isCollapsed:false,anchorNode:nodeFor(second),
                   focusNode:nodeFor(second)}
    });
    assert.equal(r.copied,beatport(322));
    // Element endpoints must resolve from THEIR OWN closest row, not
    // their parent. Crates DOM selections need not start on Text nodes.
    r=await scenario({
        all:[...first.items,...second.items],row:first,
        selection:{isCollapsed:false,
            anchorNode:{nodeType:1,closest:()=>second,
                        parentElement:{closest:()=>first}},
            focusNode:{nodeType:1,closest:()=>second,
                       parentElement:{closest:()=>first}}}
    });
    assert.equal(r.copied,beatport(322));
    r=await scenario({
        all:[...first.items,...second.items],row:first,
        selection:{isCollapsed:false,anchorNode:nodeFor(first),
                   focusNode:nodeFor(null)}
    });
    assert.equal(r.copied,'');
    assert.doesNotMatch(js,/\bfetch\s*\(/);
    assert.doesNotMatch(js,/document\.cookie\s*[;=\[]/);
    console.log('PASS: Crates visible single-link browser helper identity/safety');
})().catch(err=>{console.error(err);process.exitCode=1;});
