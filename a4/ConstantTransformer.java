import java.util.ArrayDeque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Queue;
import java.util.Set;
import java.util.TreeSet;

import soot.Body;
import soot.Local;
import soot.PrimType;
import soot.SootMethod;
import soot.Unit;
import soot.Value;
import soot.ValueBox;
import soot.jimple.internal.AbstractBinopExpr;
import soot.jimple.toolkits.callgraph.CallGraph;
import soot.toolkits.graph.BriefUnitGraph;
import soot.toolkits.graph.PseudoTopologicalOrderer;
import soot.toolkits.scalar.LocalDefs;
import soot.toolkits.scalar.SimpleLocalDefs;

public class ConstantTransformer {
    private static CallGraph cg;
    private static HashSet<SootMethod> pureUserDefinedMethods;

    public static void transformProgram(SootMethod mainMethod, ConstantPropagation cp, CallGraph _cg, HashSet<SootMethod> pureUserDefinedMethods){
        cg = _cg;
        ConstantTransformer.pureUserDefinedMethods = pureUserDefinedMethods;
        Body body = mainMethod.getActiveBody();
        BriefUnitGraph graph = new BriefUnitGraph(body);
        LocalDefs localDefs = new SimpleLocalDefs(graph);
        List<Unit> units = (new PseudoTopologicalOrderer<Unit>()).newList(graph, true);
        Queue<Unit> essentialUnits = new ArrayDeque<>();
        for(Unit unit:units){
            transformUnit(unit,cp);
            if(!canRemoveUnit(unit,cp)){
                essentialUnits.add(unit);
            }
        }
        Set<Unit> essentialUnitsSet = new HashSet<>();
        while(!essentialUnits.isEmpty()){
            Unit unit = essentialUnits.remove();
            if(essentialUnitsSet.add(unit)){
                List<ValueBox> boxes = unit.getUseBoxes();
                for(ValueBox b:boxes){
                    Value v = b.getValue();
                    if(v instanceof Local){
                        Local local  =(Local)v;    
                        List<Unit> defs = localDefs.getDefsOfAt(local, unit);
                        if(defs!=null){
                            essentialUnits.addAll(defs);
                        }
                    }
                }
            };
        }
        body.getUnits().retainAll(essentialUnitsSet);
        /*
         backwards:
         for unit in body...
             for use in useboxes:
                if(cp.before(unit).get(use) is constant){
                    replace use by constant value...
                }

                //TODO: confirm this means use boxes of def unit are removed.

            def = defboxes(unit).first
            if(def.uses.size()==0){
                body.remove(unit)
            }
        */
    }

    private static boolean canRemoveUnit(Unit unit, ConstantPropagation cp) {
        HashMap<Local, ConstantPropagation.ConstantValue> out = cp.getFlowAfter(unit);
        List<ValueBox> defBoxes = unit.getDefBoxes();
        if(defBoxes.size()>0){
            //assignment unit
            boolean definesNonConstant = false;
            for(ValueBox b:defBoxes){
                Value v = b.getValue();
                if(!ConstantPropagation.localOrLiteralIsConstant(v, out)){
                    definesNonConstant = true;
                    break;
                }
            }
            if(definesNonConstant){
                return false;
            }
            if(!Utils.isInvokeStmt(unit)){
                //some arithmetic expression on rhs
                //=> delete if non-constant.
                return true;
            }
            else{
                //invoke stmt. check purity and constancy of 
                TreeSet<SootMethod> methods = Utils.getSootMethodsFromInvokeUnit(unit, cg);
                for(SootMethod method:methods){
                    if(!pureUserDefinedMethods.contains(method)){
                        return false;
                    }
                }
                return true;
            }

        }
        return false;
    }

    private static void transformUnit(Unit unit, ConstantPropagation cp) {
        HashMap<Local, ConstantPropagation.ConstantValue> in = cp.getFlowBefore(unit);
        List<ValueBox> useBoxes = unit.getUseBoxes();
        
        for(ValueBox b:useBoxes){
            Value v = b.getValue();
            if(v instanceof AbstractBinopExpr){
                AbstractBinopExpr expr = (AbstractBinopExpr)v;
                Value op1 = expr.getOp1();
                Value op2 = expr.getOp2();
                if(ConstantPropagation.localOrLiteralIsConstant(op2, in) &&
                ConstantPropagation.localOrLiteralIsConstant(op1, in)){
                    try{
                        op1 = ConstantPropagation.extractConstantFromLiteralOrLocal(in, op1);
                        op2 = ConstantPropagation.extractConstantFromLiteralOrLocal(in, op2);
                    }
                    catch(Exception e){
                        System.out.println("Error transforming unit!");
                        e.printStackTrace();
                        return;
                    }
                    expr.setOp1(op1);
                    expr.setOp2(op2);
                }
                break;
            }
            else{
                //handle params of function calls.
                if(v.getType() instanceof PrimType){
                    if(ConstantPropagation.localOrLiteralIsConstant(v,in)){
                        try {
                            v = ConstantPropagation.extractConstantFromLiteralOrLocal(in, v);
                        } catch (Exception e) {
                            System.out.println("Error transforming unit!");
                            e.printStackTrace();
                        }
                        b.setValue(v);
                    }
                }
            }
        }       
        
    }
}
