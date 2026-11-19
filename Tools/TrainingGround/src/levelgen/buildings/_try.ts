import { planBands, planToAscii } from './bandPlan.ts';
const show = (L:number,D:number,core:any,seed:number)=>{const p=planBands({L,D,archetype:'office',core,seed});console.log(L,D,JSON.stringify(core),seed,p.variant,p.fallback??'');console.log(planToAscii(p));};
show(1800,1200,{u0:0,depth:350},1);
show(1700,1000,{u0:400,depth:350},1);
show(1200,900,null,1);
show(1200,900,null,2);
show(1400,1000,{u0:500,depth:200},3);
show(1300,800,null,3);
