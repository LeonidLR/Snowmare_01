#include "UI/Frontend/CodexDefaultTree.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

UTextBlock* CodexDefaultTree::MakeText(UWidgetTree& Tree, FName Name, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold)
{
	UTextBlock* Block = Tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	FSlateFontInfo Font = Block->GetFont();
	Font.Size = Size;
	Font.TypefaceFontName = bBold ? TEXT("Bold") : TEXT("Regular");
	Block->SetFont(Font);
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetText(Text);
	if (Name != NAME_None)
	{
		MarkVariable(Block);
	}
	return Block;
}

void CodexDefaultTree::MarkVariable(UWidget* Widget)
{
	if (Widget)
	{
		Widget->bIsVariable = true;
	}
}
