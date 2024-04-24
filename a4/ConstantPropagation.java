import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.TreeSet;

import soot.Local;
import soot.SootMethod;
import soot.Unit;
import soot.Value;
import soot.jimple.Constant;
import soot.jimple.DoubleConstant;
import soot.jimple.FloatConstant;
import soot.jimple.IntConstant;
import soot.jimple.InvokeExpr;
import soot.jimple.LongConstant;
import soot.jimple.NullConstant;
import soot.jimple.NumericConstant;
import soot.jimple.StringConstant;
import soot.jimple.internal.AbstractBinopExpr;
import soot.jimple.internal.AbstractJimpleFloatBinopExpr;
import soot.jimple.internal.JAssignStmt;
import soot.jimple.toolkits.callgraph.CallGraph;
import soot.jimple.toolkits.scalar.Evaluator;
import soot.toolkits.graph.BriefUnitGraph;
import soot.toolkits.scalar.ForwardFlowAnalysis;

public class ConstantPropagation extends ForwardFlowAnalysis<Unit,HashMap<Local,ConstantPropagation.ConstantValue>> {
    public static class ConstantValue{
        boolean isTop = true;
        boolean isBot = false;
        Value value;
        ConstantValue(Value v){
            value = v;
            isTop = false;
            // valueType=int.class;
        }
        ConstantValue(){}
        public ConstantValue(ConstantPropagation.ConstantValue constantValue) {
            isTop = constantValue.isTop;
            isBot = constantValue.isBot;
            value = constantValue.value;
        }
        static ConstantValue makeBot(){
            ConstantValue ans = new ConstantValue();
            ans.isTop = false;
            ans.isBot = true;
            return ans;
        }
        ConstantValue meet(ConstantValue cv2){
            ConstantValue ans = new ConstantValue();
            if(this.isBot || cv2.isBot){
                ans = makeBot();
            }
            else if(this.isTop && cv2.isTop){
                ans.isTop = true;//true by default
            }
            else if(this.isTop||cv2.isTop){
                if(this.isTop){
                    ans = new ConstantValue(cv2.value);
                }
                else{
                    ans = new ConstantValue(this.value);
                }
            }
            else{
                if(this.value == cv2.value){
                    ans = new ConstantValue(this.value);
                }
                else{
                    ans = makeBot();
                }
            }
            return ans;
        }
        // ConstantValue(String v){
        //     strValue = v;
        //     isTop = false;
        //     valueType = String.class;
        // }
        boolean isConstant(){
            return !(isTop||isBot);
        }
        @Override
        public String toString() {
            String ans = "";
            if(isTop){
                ans = "top";
            }
            else if(isBot){
                ans = "bot";
            }
            else{
                ans = value.toString();
            }
            return ans;
        }
    }
    ArrayList<Local> locals;
    HashSet<SootMethod> pureMethods;
    CallGraph cg;
    ConstantPropagation(BriefUnitGraph g, ArrayList<Local> _locals, HashSet<SootMethod> _pureMethods, CallGraph _cg){
        super(g);
        locals = _locals;
        pureMethods = _pureMethods;
        cg = _cg;
        this.doAnalysis();
    }
    public static boolean valueIsConstant(Value v){
        if(v instanceof NumericConstant || v instanceof StringConstant || v instanceof NullConstant){
            return true;
        }
        return false;
    }

    @Override
    protected void flowThrough(HashMap<Local, ConstantPropagation.ConstantValue> in, Unit unit, HashMap<Local, ConstantPropagation.ConstantValue> out) {
        copy(in,out);
        if(unit instanceof JAssignStmt){
            Value lhs, rhs;
            JAssignStmt stmt = (JAssignStmt) unit;
            lhs = stmt.getLeftOp();
            rhs = stmt.getRightOp();
            if(lhs instanceof Local){
                if(valueIsConstant(rhs)){
                    out.put((Local)lhs, new ConstantValue(rhs));
                }
                else if(rhs instanceof Local){
                    out.put((Local)lhs, new ConstantValue(in.get((Local)rhs)));
                }
                else if(rhs instanceof AbstractBinopExpr){
                    AbstractBinopExpr expr = ((AbstractBinopExpr)rhs);
                    ConstantValue cv1 = new ConstantValue(expr.getOp1());
                    ConstantValue cv2 = new ConstantValue(expr.getOp2());
                    ConstantValue ans = new ConstantValue();
                    if(cv1.value instanceof Local){
                        cv1 = in.get((Local)cv1.value);
                    }
                    if(cv2.value instanceof Local){
                        cv2 = in.get((Local)cv2.value);
                    }
                    if(cv1.isBot || cv2.isBot){
                        ans = ConstantValue.makeBot();
                    }
                    else if(cv1.isTop || cv2.isTop){
                        //remains top
                    }
                    else{
                        //both are non-top, non-bot values.
                        expr = (AbstractJimpleFloatBinopExpr)expr.clone();
                        expr.setOp1(cv1.value);
                        expr.setOp2(cv2.value);
                        ans = new ConstantValue(Evaluator.getConstantValueOf(expr));
                    }
                    out.put((Local)lhs, ans);
                }
                else if(Utils.isInvokeExpression(rhs)){
                    InvokeExpr expr = Utils.getInvokeExprFromInvokeUnit(unit);
                    TreeSet<SootMethod> methods = Utils.getSootMethodsFromInvokeUnit(unit, cg);
                    List<Value> args = (expr.getArgs());
                    System.out.println("invoke: "+unit.toString()+", #methods: "+methods.size());
                    if(!argsAreConstant(args,in)){
                        out.put((Local)lhs, ConstantValue.makeBot());
                        System.out.println("non constant args");
                    }
                    else{
                        //merge constant values across possible callees...
                        ConstantValue cv = new ConstantValue();
                        for(SootMethod method:methods){
                            ConstantValue methodCV = getConstantValueFromMethodEval(in, lhs, args, method);
                            cv = cv.meet(methodCV);
                        }
                        out.put((Local)lhs,cv);
                    }
                }
            }
        }
        else{
            //ignoring all other types of statements
        }
    }
    private ConstantValue getConstantValueFromMethodEval(HashMap<Local, ConstantPropagation.ConstantValue> in, Value lhs, List<Value> args,
            SootMethod method) {
        ConstantValue cv;
        if(!pureMethods.contains(method)){
            cv = ConstantValue.makeBot();
        }
        else{
            System.out.println("evaluating...");
            Value value = evaluatePureMethodCall(method,args,in,lhs);
            if(value!=null){
                cv = new ConstantValue(value);
            }
            else{
                cv = ConstantValue.makeBot();
            }
        }
        return cv;
    }

    private Value evaluatePureMethodCall(SootMethod method, List<Value> args, HashMap<Local, ConstantPropagation.ConstantValue> in, Value lhs) {
        try{
            Value ans;
            Method javaMethod = Utils.convertSootToJavaMethod(method);
            javaMethod.setAccessible(true);
            List<Object> jargs = getJavaArgs(args, in);
            Object result = javaMethod.invoke(null,jargs.toArray());
            ans = getSootValueFromJavaResult(result,lhs);
            return ans;
        }
        catch(Exception e){
            System.out.println("Exception in evaluation: "+e.toString());
            e.printStackTrace();
            return null;
        }
    }
    private List<Object> getJavaArgs(List<Value> args, HashMap<Local, ConstantPropagation.ConstantValue> in)
            throws Exception {
        List<Object> jargs = new ArrayList<>();
        for(Value arg:args){
            Value value = extractConstantFromLiteralOrLocal(in, arg);
            jargs.add(getJavaValueFromSootValue(value));
        }
        return jargs;
    }
    public static Value extractConstantFromLiteralOrLocal(HashMap<Local, ConstantPropagation.ConstantValue> in, Value arg)
            throws Exception {
        if(arg instanceof Local){
            Local local = (Local)arg;
            return (in.get(local).value);
        }
        else if(valueIsConstant(arg)){
            return arg;
        }
        else{
            throw new Exception("Non-constant arg passed to evaluatePureMethodCall");
        }
    }
    private Value getSootValueFromJavaResult(Object result, Value lhs) {
        Value ans = IntConstant.v(0);
        if (result instanceof Integer) {
            ans = IntConstant.v((int) result);
        } else if (result instanceof Long) {
            ans = LongConstant.v((long) result);
        } else if (result instanceof Float) {
            ans = FloatConstant.v((float) result);
        } else if (result instanceof Double) {
            ans = DoubleConstant.v((double) result);
        } else if (result instanceof String){
            ans = StringConstant.v((String)result);
        }
        return ans;
    }
    private Object getJavaValueFromSootValue(Value arg) throws Exception{
        if(arg instanceof Constant){
            if (arg instanceof IntConstant) {
                return ((IntConstant) arg).value;
            } else if (arg instanceof LongConstant) {
                return ((LongConstant) arg).value;
            } else if (arg instanceof FloatConstant) {
                return ((FloatConstant) arg).value;
            } else if (arg instanceof DoubleConstant) {
                return ((DoubleConstant) arg).value;
            } else if (arg instanceof StringConstant) {
                return ((StringConstant) arg).value;
            } else if (arg instanceof NullConstant) {
                return null;
            } else{
                throw new Exception("Unhandled constant type arg encountered: "+arg.getClass().toString());
            }
        }
        else{
            throw new Exception("Man, why you be asking the value of a non-constant?");
        }
    }
    private boolean argsAreConstant(List<Value> args, HashMap<Local, ConstantPropagation.ConstantValue> in) {
        for(Value arg:args){
            if(!localOrLiteralIsConstant(arg,in)){
                return false;
            }
        }
        return true;
    }
    public static boolean localOrLiteralIsConstant(Value arg, HashMap<Local,ConstantPropagation.ConstantValue> in) {
        if(arg instanceof Local){
            Local local = (Local)arg;
            ConstantPropagation.ConstantValue dfav = in.get(local);
            if(dfav.isConstant()){
                return true;
            }
        }
        else if(valueIsConstant(arg)){
            return true;
        }
        return false;
    }
    @Override
    protected void copy(HashMap<Local, ConstantPropagation.ConstantValue> src, HashMap<Local, ConstantPropagation.ConstantValue> dst) {
        dst.clear();
        for(Local local :src.keySet()){
            dst.put(local,new ConstantValue(src.get(local)));
        }
    }

    @Override
    protected void merge(HashMap<Local, ConstantPropagation.ConstantValue> in1, HashMap<Local, ConstantPropagation.ConstantValue> in2, HashMap<Local, ConstantPropagation.ConstantValue> out) {
        for(Local local:in1.keySet()){
            ConstantPropagation.ConstantValue cv1 = in1.get(local);
            ConstantPropagation.ConstantValue cv2 = in2.get(local);
            out.put(local, cv2.meet(cv1));
        }
        
    }

    @Override
    protected HashMap<Local, ConstantPropagation.ConstantValue> newInitialFlow() {
        HashMap<Local,ConstantPropagation.ConstantValue> ans = new HashMap<>();
        for(Local local:locals){
            //put in top values.
            ans.put(local, new ConstantPropagation.ConstantValue());
        }
        return ans;
    }
    public void printAnalysis() {
        this.graph.forEach((Unit unit)->{
            synchronized(System.out){
                System.out.println(unit.toString() +"{");
                HashMap<Local, ConstantPropagation.ConstantValue> flow = this.getFlowBefore(unit);
                System.out.println("\tin: {");
                for(Local local:flow.keySet()){
                    System.out.println("\t\t"+local.toString()+"->"+flow.get(local).toString()+",");
                }
                System.out.println("\t},");
                System.out.println("\tout: {");
                flow = this.getFlowAfter(unit);
                for(Local local:flow.keySet()){
                    System.out.println("\t\t"+local.toString()+"->"+flow.get(local).toString()+",");
                }
                System.out.println("\t}\n}");
            }
        });
    }


}
