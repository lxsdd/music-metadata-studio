// Run the shipping Crates release bookmarklet against a checked visible
// release-page DOM shape; no network requests, login or hidden state.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const path=require('node:path');
const {TextEncoder}=require('node:util');
const script=fs.readFileSync(path.join(__dirname,
  '../scripts/crates-copy-visible-release-facts.js'),'utf8');
const id='3939820';
const releasePath='/release/glow/'+id;
const releaseUrl='https://crates.co'+releasePath;
const beatportUrl='https://www.beatport.com'+releasePath;
function el(label='',children={},more={}) {
  return {innerText:label,parentElement:null,hidden:false,inert:false,
    getClientRects:()=>[{}],getAttribute:()=>null,
    querySelectorAll:s=>children[s]||[],...more};
}
function row(trackId,number,title,artist,bpm,key,duration,overrides={}) {
  const route='/track/'+(number===1?'glow':'stella')+'/'+trackId;
  const titleLink=el(title,{},{hostname:'crates.co',pathname:route});
  const artistLinks=artist.map(x=>el(x));
  const label=el('Siona Records');
  const album=el(overrides.foreignAlbum?'Other EP':'Glow');
  const cells=[
    el('✓'),el('►'),el(String(number)),
    el('',{'a[href]':[titleLink]}),
    el('',{'a[href]':artistLinks}),
    el('',{'a[href]':[]}),
    el('',{'a[href]':[label]}),
    el('',{'a[href]':[album]}),
    el(key),el(bpm),el(duration),el('2022-12-09')
  ];
  return el('',{':scope > td':cells},{
    getAttribute:name=>name==='data-trackid'?trackId:null
  });
}
async function sample(overrides={}) {
  let copied='',message='';
  const title=el('Glow',{},{hostname:'crates.co',pathname:releasePath});
  const label=el('Siona Records');
  const info=el('Siona Records\nSNA146',{'a[href]':[label]});
  const bp=el('View on Beatport',{},{href:overrides.bpUrl||
    beatportUrl+'?a_aid=example&utm_source=bp_affiliate'});
  const profile=el('',{
    'h2.cratePicker a.itemLink[href]':[title],
    '.catalogInfo':[info],
    'p.beatportLink a[href*="beatport.com/release/"]':[bp]
  });
  const rows=overrides.rows||[
    row('17136572',1,'Glow (Extended Mix)',
      ['Key M','Cherry (UA)','Beat Inside'],'123','10B','6:28',
      {foreignAlbum:overrides.foreignAlbum}),
    row(overrides.duplicate?'17136572':'17136574',2,
      'Stella (Extended Mix)',['Cherry (UA)'],
      '124','7B',overrides.duration||'6:55')
  ];
  const context={
    location:{protocol:'https:',hostname:'crates.co',port:'',
      search:overrides.search||'',hash:'',pathname:overrides.path||releasePath},
    document:{querySelectorAll:selector=>{
      if(selector==='.profile.release h2.cratePicker a.itemLink[href]') return [title];
      if(selector==='.profile.release') return [profile];
      if(selector==='table.tracks tr[data-trackid]') return rows;
      return [];
    }},
    getComputedStyle:()=>({display:'block',visibility:'visible',opacity:'1'}),
    URL,TextEncoder,
    navigator:{clipboard:{writeText:async v=>{copied=v;}}},
    alert:v=>{message=v;}
  };
  await vm.runInNewContext(script,context,{timeout:1500});
  return {copied,message};
}
(async()=>{
  const good=await sample();
  assert.match(good.copied,
    /^@crates-public-release\n@url=https:\/\/crates\.co\/release\/glow\/3939820\n@beatport-url=https:\/\/www\.beatport\.com\/release\/glow\/3939820\n/);
  assert.match(good.copied,/ALBUM=Glow\nLABEL=Siona Records\nCATALOGNUMBER=SNA146\n@track\n/);
  assert.match(good.copied,/RELEASE_ID=3939820\nTRACKNUMBER=1\nTITLE=Glow \(Extended Mix\)\nARTIST=Key M\nARTIST=Cherry \(UA\)\nARTIST=Beat Inside\nDURATION=6:28\nBPM=123\nKEY=10B/);
  assert.match(good.copied,/TRACKNUMBER=2\nTITLE=Stella \(Extended Mix\)\nARTIST=Cherry \(UA\)\nDURATION=6:55\nBPM=124\nKEY=7B/);
  assert.doesNotMatch(good.copied,/DATE=|DATE@|2022-12-09|affiliate/);
  for(const o of [
    {bpUrl:'https://www.beatport.com/release/other/3939821'},
    {bpUrl:beatportUrl+'?token=secret'},
    {path:'/release/other/3939820'},
    {search:'?from=other'},
    {duplicate:true},
    {foreignAlbum:true},
    {duration:'bad'}
  ]) {
    const bad=await sample(o);
    assert.equal(bad.copied,'','unqualified release: '+JSON.stringify(o));
    assert.match(bad.message,/Crates visible release \(read-only\):/);
  }
  assert.doesNotMatch(script,/\bfetch\s*\(/);
  assert.doesNotMatch(script,/document\.cookie|localStorage|sessionStorage/);
  console.log('PASS: Crates visible release album/catalog + complete tracklist read-only');
})().catch(error=>{console.error(error);process.exitCode=1;});
