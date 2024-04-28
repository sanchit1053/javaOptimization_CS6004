
public class T6 {
    public static class A{
        int foo(int i){
            return (i+1)*20;
        }
        public static void main(String[] args) {
            
        }
    }
    public static class B extends A{
        int foo(int i){
            if(i==0){return 1;}
            return i*foo(i-1);
        }
        public static void main(String[] args) {
            
        }
    }
    static A obj;
    public static void main(String[] args) {
        int i = 5;
        obj = new B();
        i = getFactorial(i);
        System.out.println(i);
    }
    public static int getFactorial(int i){
        return obj.foo(i);
    }
}
