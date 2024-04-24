import java.lang.reflect.Array;
import java.lang.reflect.Method;
import java.util.Comparator;
import java.util.Iterator;
import java.util.TreeSet;
import java.util.regex.Pattern;

import soot.ArrayType;
import soot.PrimType;
import soot.RefType;
import soot.SootMethod;
import soot.Type;
import soot.Unit;
import soot.Value;
import soot.jimple.InvokeExpr;
import soot.jimple.internal.AbstractInstanceInvokeExpr;
import soot.jimple.internal.JAssignStmt;
import soot.jimple.internal.JDynamicInvokeExpr;
import soot.jimple.internal.JInterfaceInvokeExpr;
import soot.jimple.internal.JInvokeStmt;
import soot.jimple.internal.JSpecialInvokeExpr;
import soot.jimple.internal.JStaticInvokeExpr;
import soot.jimple.internal.JVirtualInvokeExpr;
import soot.jimple.toolkits.callgraph.CallGraph;
import soot.jimple.toolkits.callgraph.Edge;

public class Utils {
    public static boolean isInvokeExpression(Value expression) {
        return expression instanceof JInterfaceInvokeExpr || expression instanceof JVirtualInvokeExpr
                || expression instanceof JStaticInvokeExpr || expression instanceof JDynamicInvokeExpr
                || expression instanceof JSpecialInvokeExpr;
    }
    public static Value getReceiverOfInvokeUnit(Unit u){
        InvokeExpr expr = Utils.getInvokeExprFromInvokeUnit(u);
        if(expr instanceof JVirtualInvokeExpr || expr instanceof JInterfaceInvokeExpr){
            AbstractInstanceInvokeExpr iiexpr =(AbstractInstanceInvokeExpr)expr;
            return iiexpr.getBase();
        }
        return null;
    }
    public static boolean isInvokeStmt(Unit u) {
        boolean ans = false;
        if (u instanceof JAssignStmt) {
            Value rhs = ((JAssignStmt) u).getRightOp();
            if (isInvokeExpression(rhs)) {
                ans = true;
            }
        } else if (u instanceof JInvokeStmt) {
            JInvokeStmt stmt = (JInvokeStmt) (u);
            InvokeExpr expr = stmt.getInvokeExpr();
            if (isInvokeExpression(expr)) {
                ans = true;
            }
        }
        return ans;
    }
    public static class SootMethodComparator implements Comparator<SootMethod>{

        @Override
        public int compare(SootMethod o1, SootMethod o2) {
            return o1.toString().compareTo(o2.toString());
        }
        
    }
    public static TreeSet<SootMethod> getSootMethodsFromInvokeUnit(Unit u, CallGraph cg) {
        TreeSet<SootMethod> ans = new TreeSet<>(new SootMethodComparator());
        for (Iterator<Edge> iter = cg.edgesOutOf(u); iter.hasNext();) {
            Edge edge = iter.next();
            SootMethod tgtMethod = edge.tgt();
            ans.add(tgtMethod);
        }
        return ans;
    }
    public static InvokeExpr getInvokeExprFromInvokeUnit(Unit u) {
        InvokeExpr ans;
        if (u instanceof JAssignStmt) {
            Value rhs = ((JAssignStmt) u).getRightOp();
            InvokeExpr expr = (InvokeExpr) rhs;
            ans = expr;
        } else {
            // u instanceof JInvokeStmt
            JInvokeStmt stmt = (JInvokeStmt) (u);
            InvokeExpr expr = stmt.getInvokeExpr();
            ans = expr;
        }
        return ans;
    }
    public static Method convertSootToJavaMethod(SootMethod sootMethod) throws ClassNotFoundException, NoSuchMethodException {
        // Get the declaring class of the SootMethod
        String className = sootMethod.getDeclaringClass().getName();
        Class<?> declaringClass = Class.forName(className);

        // Get the method name
        String methodName = sootMethod.getName();

        //TODO: remove if unnecessary.
        if(methodName.contains(".")){
            String[] parts = methodName.split(Pattern.quote("."));
            methodName = parts[parts.length-1];
        }
        // Get the parameter types of the method
        Class<?>[] parameterTypes = new Class<?>[sootMethod.getParameterCount()];
        for (int i = 0; i < sootMethod.getParameterCount(); i++) {
            parameterTypes[i] = getClassForType(sootMethod.getParameterType(i));
        }

        // Find and return the corresponding Method using reflection
        // for(Method method: declaringClass.getDeclaredMethods()){
        //     System.out.print(method.getName()+": ");
        //     for(Class type:method.getParameterTypes()){
        //         System.out.print(type.toString()+",");
        //     }
        //     System.out.print("\n");
        // }
        return declaringClass.getDeclaredMethod(methodName, parameterTypes);
    }
    public static Class<?> getClassForType(Type sootType) throws ClassNotFoundException {
        if (sootType instanceof RefType) {
            RefType refType = (RefType) sootType;
            String className = refType.getClassName();
            return Class.forName(className);
        } else if (sootType instanceof PrimType) {
            PrimType primType = (PrimType) sootType;
            switch (primType.toString()) {
                case "int":
                    return int.class;
                case "byte":
                    return byte.class;
                case "short":
                    return short.class;
                case "long":
                    return long.class;
                case "float":
                    return float.class;
                case "double":
                    return double.class;
                case "boolean":
                    return boolean.class;
                case "char":
                    return char.class;
                default:
                    throw new IllegalArgumentException("Unsupported primitive type: " + primType.toString());
            }
        } else if (sootType instanceof ArrayType) {
            ArrayType arrayType = (ArrayType) sootType;
            Class<?> elementType = getClassForType(arrayType.baseType);
            return Array.newInstance(elementType, 0).getClass();
        } else {
            throw new IllegalArgumentException("Unsupported type: " + sootType.getClass().getName());
        }
    }

}
