import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

import soot.Body;
import soot.Local;
import soot.Scene;
import soot.SceneTransformer;
import soot.SootClass;
import soot.SootMethod;
import soot.jimple.toolkits.callgraph.CallGraph;
import soot.toolkits.graph.BriefUnitGraph;
import soot.util.Chain;
public class AnalysisTransformer extends SceneTransformer{
    static CallGraph cg;
    static HashSet<SootMethod> userDefinedMethods = new HashSet<>();
    AnalysisTransformer(String tcdir){
        // loadClassFiles(tcdir);
    }
    @Override
    protected void internalTransform(String arg0, Map<String, String> arg1) {
        cg = Scene.v().getCallGraph();
        Set<SootMethod> mainMethods = fillUserDefMethodsAndGetMainMethods();
        HashSet<SootMethod> impureMethods = PureMethodClassifier.pureMethodClassification(mainMethods, cg,userDefinedMethods);
        // System.out.println("Impure methods: ");
        // for(SootMethod method:impureMethods){
        //     System.out.println(method.toString()+", ");
        // }
        HashSet<SootMethod> pureUserDefinedMethods = new HashSet<>(userDefinedMethods);
        for(SootMethod method:impureMethods){
            pureUserDefinedMethods.remove(method);
        }
        // System.out.println("Pure methods: ");
        // for(SootMethod method:pureUserDefinedMethods){
        //     System.out.println(method.toString()+", ");
        // }
        for(SootMethod mainMethod:mainMethods){
            methodTransform(mainMethod.getActiveBody(),pureUserDefinedMethods);
        }
        
    }

    private Set<SootMethod> fillUserDefMethodsAndGetMainMethods() {
        // Get the main method
        Set<SootMethod> mainMethods = new HashSet<>();
        Chain<SootClass> classes = Scene.v().getClasses();
        for(SootClass classInstance:classes){
            //ignore java lang classes
            if(!classInstance.isApplicationClass()){
                continue;
            }
            List<SootMethod> methods = classInstance.getMethods();
            for(SootMethod method:methods){
                if(!method.isConstructor()){
                    if(method.isMain()){
                        mainMethods.add(method);
                    }
                    userDefinedMethods.add(method);
                }
            }
        }
        // Queue<SootMethod> reachables = new ArrayDeque<>(mainMethods);
        // while(reachables.size()>0){
        //     SootMethod method = reachables.remove();
        //     Iterator<Edge> iter = cg.edgesOutOf(method);
        //     while(iter.hasNext()){
        //         Edge e = iter.next();
        //         if(e.tgt().getDeclaringClass().isApplicationClass()){
        //             if(!userDefinedMethods.contains(e.tgt())){
        //                 userDefinedMethods.add(e.tgt());
        //                 reachables.add(e.tgt());
        //             }
        //         }
        //     }
        // }
        System.out.println("UDMS: ");
        for(SootMethod method:userDefinedMethods){
            System.out.println("\t"+method.toString());
        }
        return mainMethods;
    }
    public static class SootMethodComparator implements Comparator<SootMethod>{

        @Override
        public int compare(SootMethod o1, SootMethod o2) {
            return o1.toString().compareTo(o2.toString());
        }
        
    }
    
    protected void methodTransform(Body body, HashSet<SootMethod> pureUserDefinedMethods) {
        System.out.println("Processing: "+body.getMethod().toString());
        ArrayList<Local> locals = new ArrayList<Local>(body.getLocals());
        ConstantPropagation cp = new ConstantPropagation(new BriefUnitGraph(body), locals, pureUserDefinedMethods,cg);
        // cp.printAnalysis();
        ConstantTransformer.transformProgram(body.getMethod(), cp,cg,pureUserDefinedMethods);
        //TODO: transform, cp in while loop while body changes?
    }
    void printSet(Set<?> set){
        for(Object obj:set){
            print(obj.toString()+", ");
        }
        print("\n");
    }
    private void print(String u) {
        System.out.println(u);
    }
    
}