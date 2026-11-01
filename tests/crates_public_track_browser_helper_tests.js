// Exercise shipping Crates DOM scraper against a verified public track-page
// shape. No network, browser cookies, credentials or mock private API calls.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const {TextEncoder}=require('node:util');
const script=fs.readFileSync(
  path.join(__dirname,'../scripts/crates-copy-visible-track-facts.js'),'utf8');
const id='17136574';
const route='/track/stella/'+id;
const crate='https://crates.co'+route;
const beatport='https://www.beatport.com'+route;
function el(label='',items={},extra={}) {
  return {
    textContent:label,innerText:label,
    parentElement:null,hidden:false,inert:false,
    getClientRects:()=>[{}],
    getAttribute:()=>null,
    querySelectorAll:s=>items[s]||[],
    ...extra
  };
}
function textCell(value) { return el(value); }
async function sample(overrides={}) {
  let copied='',message='';
  const artist=el('Cherry (UA)');
  const label=el('Siona Records');
  const album=el('Glow');
  const track=el(overrides.title||'Stella (Extended Mix)',{},{
    href:crate,pathname:route,hostname:'crates.co'
  });
  const elements=[
    el(),el(),el('',{'a[href]':[track]}),
    el('',{'a[href]':[artist]}),el('',{'a[href]':[]}),
    el('',{'a[href]':[label]}),el('',{'a[href]':[album]}),
    textCell('7B'),textCell(overrides.bpm||'124'),
    textCell(overrides.duration||'6:55'),textCell('2022-12-09')
  ];
  const row=el('',{':scope > td':elements});
  const bp=el('View on Beatport',{},{
    href:overrides.beatport||beatport+'?a_aid=affiliate&utm_source=bp_affiliate',
  });
  const subtitle=el(overrides.mix||'Extended Mix');
  const genre=el('Melodic House & Techno');
  const profile=el('',{
    'a[href*="beatport.com/track/"]':[bp],
    'h2 .subtitle':overrides.noMix?[]:[subtitle],
    'a.genreLink':[genre]
  });
  const context={
    location:{protocol:'https:',hostname:overrides.host||'crates.co',
      port:'',search:overrides.search||'',hash:'',
      pathname:overrides.pathname||route},
    document:{
      querySelectorAll:s=>{
        if(s.startsWith('table.tracks tr[data-trackid=')) return [row];
        if(s==='.profile.track') return [profile];
        return [];
      }
    },
    getComputedStyle:()=>({display:'block',visibility:'visible',opacity:'1'}),
    URL,TextEncoder,
    navigator:{clipboard:{writeText:async value=>{copied=value;}}},
    alert:value=>{message=value;}
  };
  await vm.runInNewContext(script,context,{timeout:1500});
  return {copied,message};
}
(async()=>{
  const good=await sample();
  assert.match(good.copied,/^@crates-public-track\n@url=https:\/\/crates\.co\/track\/stella\/17136574\n@beatport-url=https:\/\/www\.beatport\.com\/track\/stella\/17136574\n/);
  assert.match(good.copied,/TITLE=Stella \(Extended Mix\)\nARTIST=Cherry \(UA\)\nVERSION=Extended Mix\nDURATION=6:55\nBPM=124\nKEY=7B\nLABEL=Siona Records\nALBUM=Glow\nGENRE=Melodic House & Techno/);
  assert.doesNotMatch(good.copied,/DATE=|DATE@|2022-12-09|affiliate/);
  for(const data of [
    {beatport:'https://www.beatport.com/track/stella/17136575'},
    {beatport:beatport+'?token=secret'},
    {host:'evil-crates.co'},
    {search:'?track=else'},
    {title:'Stella (Radio Edit)'},
    {duration:'broken'},
    {bpm:'999'},
    {pathname:'/track/stella/17136575'}
  ]) {
    const bad=await sample(data);
    assert.equal(bad.copied,'','invalid Crates page must not be copied: '+
      JSON.stringify(data));
    assert.match(bad.message,/Crates visible track \(read-only\):/);
  }
  const noMix=await sample({noMix:true});
  assert.doesNotMatch(noMix.copied,/VERSION=/);
  assert.doesNotMatch(script,/\bfetch\s*\(/);
  assert.doesNotMatch(script,/document\.cookie|localStorage|sessionStorage/);
  console.log('PASS: Crates visible DOM exact source/track, no date inference');
})().catch(error=>{console.error(error);process.exitCode=1;});
