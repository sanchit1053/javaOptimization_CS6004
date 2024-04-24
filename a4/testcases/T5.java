
public class T5 {
    public static void main(String[] args) {
        int i = 5;
        System.out.println(factorial(i));
    }

    private static int factorial(int i) {
        if(i==0){
            return 1;
        }
        System.out.println("called with: "+i);
        return i*factorial(i-1);
    }
}
